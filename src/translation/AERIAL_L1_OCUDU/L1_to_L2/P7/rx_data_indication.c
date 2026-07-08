// Copyright 2024-2026 coRAN LABS Private Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// SCF RX_DATA.indication -> OCUDU RX_DATA_INDICATION (0x85).
// Aerial keeps the PDU headers in msg_buf and concatenates the transport blocks
// in data_buf, in PDU order; pdu_length is a 32-bit field. ul_cqi/ta/rssi are
// dropped, the same quality metrics arrive in CRC.indication.

#include "aerial_l1_to_l2_p7.h"

#ifdef AERIAL_OCUDU

#include <stdio.h>
#include <stdlib.h>

#include "app_context.h"
#include "aerial_l1_to_l2_p5.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "message_stats.h"

// Little-endian cursor over an SCF buffer (bounds-checked).
typedef struct { const uint8_t* d; uint32_t len; uint32_t off; int err; } le_rd_t;

static uint8_t le_u8(le_rd_t* r)
{
    if (r->err || r->off + 1u > r->len) { r->err = 1; return 0; }
    return r->d[r->off++];
}
static uint16_t le_u16(le_rd_t* r)
{
    if (r->err || r->off + 2u > r->len) { r->err = 1; return 0; }
    uint16_t v = (uint16_t)(r->d[r->off] | ((uint16_t)r->d[r->off + 1] << 8));
    r->off += 2; return v;
}
static uint32_t le_u32(le_rd_t* r)
{
    if (r->err || r->off + 4u > r->len) { r->err = 1; return 0; }
    uint32_t v = (uint32_t)r->d[r->off] | ((uint32_t)r->d[r->off + 1] << 8)
               | ((uint32_t)r->d[r->off + 2] << 16) | ((uint32_t)r->d[r->off + 3] << 24);
    r->off += 4; return v;
}

#define AERIAL_RXDATA_MAX_TB_BYTES 65536u

int aerial_l1l2_rx_data_indication(struct AppContext* ctx,
                                   const uint8_t* scf_msg, uint32_t msg_len,
                                   const uint8_t* data_buf, uint32_t data_len)
{
    if (scf_msg == NULL || msg_len < AERIAL_SCF_MSG_HDR_SIZE + 6u) {
        SM_Logs(LOG_WARN, _P7_, "[L1->L2 P7] RX_DATA.indication too short (%u).", msg_len);
        return -1;
    }

    le_rd_t r = { scf_msg + AERIAL_SCF_MSG_HDR_SIZE,
                  msg_len - AERIAL_SCF_MSG_HDR_SIZE, 0, 0 };
    uint16_t sfn   = le_u16(&r);
    uint16_t slot  = le_u16(&r);
    uint16_t n_pdu = le_u16(&r);

    le_rd_t tb = { data_buf, (data_buf != NULL) ? data_len : 0u, 0, 0 };

    int      mu    = aerial_p7_cell_mu(ctx);
    uint32_t count = (uint32_t)sfn * (10u << mu) + slot;

    uint32_t cap  = 64u + (uint32_t)n_pdu * 16u + tb.len;
    uint8_t* body = (uint8_t*)malloc(cap);
    if (body == NULL) {
        SM_Logs(LOG_ERROR, _P7_, "[L1->L2 P7] RX_DATA malloc(%u) failed.", cap);
        return -1;
    }
    ocudu_wr_t w;
    ocudu_wr_init(&w, body, cap);

    ocudu_wr_u8(&w, 1);
    ocudu_wr_u8(&w, (uint8_t)mu);
    ocudu_wr_u32(&w, count);
    ocudu_wr_u16(&w, n_pdu);

    char content[MAX_MESSAGE_CONTENT_LEN];
    int  clen = snprintf(content, sizeof(content),
                         "message_type=RX_DATA_INDICATION\nsfn=%u\nslot=%u\nnumber_of_pdus=%u\n",
                         sfn, slot, n_pdu);

    for (uint16_t i = 0; i < n_pdu && !r.err; ++i) {
        uint32_t handle  = le_u32(&r);
        uint16_t rnti    = le_u16(&r);
        uint8_t  harq_id = le_u8(&r);
        uint32_t pdu_len = le_u32(&r);
        (void)le_u8(&r);    // ul_cqi
        (void)le_u16(&r);   // timing_advance
        (void)le_u16(&r);   // rssi
        if (r.err) break;

        if (pdu_len > AERIAL_RXDATA_MAX_TB_BYTES ||
            (uint64_t)tb.off + pdu_len > tb.len) {
            SM_Logs(LOG_WARN, _P7_,
                    "[L1->L2 P7] RX_DATA pdu %u len=%u overruns data_buf (%u); stop.",
                    i, pdu_len, tb.len);
            r.err = 1;
            break;
        }
        const uint8_t* pdu = tb.d + tb.off;
        tb.off += pdu_len;

        ocudu_wr_u32(&w, handle);
        ocudu_wr_u16(&w, rnti);
        ocudu_wr_u8(&w, harq_id);
        ocudu_wr_u32(&w, pdu_len);
        if (pdu_len > 0) ocudu_wr_bytes(&w, pdu, pdu_len);

        if (clen >= 0 && clen < (int)sizeof(content))
            clen += snprintf(content + clen, sizeof(content) - clen,
                             "----RX PDU[%u]----\nhandle=%u\nrnti=%u\nharq_id=%u\n"
                             "pdu_length=%u\n", i, handle, rnti, harq_id, pdu_len);
    }

    int rc;
    if (r.err || w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] RX_DATA.indication parse/encode error "
                "(rd_off=%u rd_len=%u wr_off=%u n_pdu=%u).",
                r.off, r.len, w.off, n_pdu);
        rc = -1;
    } else {
        add_message_stats("RX_DATA_INDICATION", (int)sfn, (int)slot,
                          (int)w.off, (int)n_pdu, content, 0);
        SM_Logs(LOG_INFO, _P7_,
                "[L1->L2 P7] RX_DATA.indication SFN %u.%u n_pdu=%u -> OCUDU-L2.",
                sfn, slot, n_pdu);
        rc = aerial_l2_xsm_put(ctx, OCUDU_FAPI_RX_DATA_INDICATION, body, w.off);
    }

    free(body);
    return rc;
}

#endif /* AERIAL_OCUDU */
