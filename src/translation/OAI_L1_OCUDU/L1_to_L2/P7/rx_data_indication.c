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

// nFAPI RX_DATA.indication (0x85) -> OCUDU RX_DATA_INDICATION.
// transport_block <- nFAPI pdu[pdu_length]; ul_cqi/ta/rssi dropped (quality is in CRC.ind).

#include "oai_l1_to_l2_p7.h"

#ifdef OAI_OCUDU

#include <stdio.h>
#include <string.h>

#include "../../main/app_context.h"
#include "oai_vnf.h"
#include "oai_l1_to_l2_p5.h"     // ocudu_l2_xsm_put()
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "nfapi_nr_interface.h"  // NFAPI_NR_P7_HEADER_LENGTH
#include "message_stats.h"

typedef struct { const uint8_t* d; uint32_t len; uint32_t off; int err; } be_rd_t;

static uint8_t be_u8(be_rd_t* r)
{
    if (r->err || r->off + 1u > r->len) { r->err = 1; return 0; }
    return r->d[r->off++];
}
static uint16_t be_u16(be_rd_t* r)
{
    if (r->err || r->off + 2u > r->len) { r->err = 1; return 0; }
    uint16_t v = (uint16_t)((r->d[r->off] << 8) | r->d[r->off + 1]);
    r->off += 2; return v;
}
static uint32_t be_u32(be_rd_t* r)
{
    if (r->err || r->off + 4u > r->len) { r->err = 1; return 0; }
    uint32_t v = ((uint32_t)r->d[r->off] << 24) | ((uint32_t)r->d[r->off + 1] << 16)
               | ((uint32_t)r->d[r->off + 2] << 8) | (uint32_t)r->d[r->off + 3];
    r->off += 4; return v;
}

// Max single UL TB we will relay; larger PDUs are dropped rather than overflowing.
#define OCUDU_RXDATA_MAX_TB_BYTES 32768u

int ocudu_l1l2_rx_data_indication(struct AppContext* ctx,
                                  const uint8_t* nfapi_msg, uint32_t len)
{
    if (nfapi_msg == NULL || len < NFAPI_NR_P7_HEADER_LENGTH + 6u) {
        SM_Logs(LOG_WARN, _P7_, "[L1->L2 P7] RX_DATA.indication too short (%u).", len);
        return -1;
    }

    be_rd_t r = { nfapi_msg + NFAPI_NR_P7_HEADER_LENGTH,
                  len - NFAPI_NR_P7_HEADER_LENGTH, 0, 0 };
    uint16_t sfn   = be_u16(&r);
    uint16_t slot  = be_u16(&r);
    uint16_t n_pdu = be_u16(&r);

    oai_vnf_t* v = ctx->oai_ocudu_ctx.vnf;
    int mu = (v != NULL && v->cell_numerology >= 0 && v->cell_numerology < 5)
                 ? v->cell_numerology : 1;
    uint32_t slots_per_frame = 10u << mu;
    uint32_t count = (uint32_t)sfn * slots_per_frame + slot;

    // TBs are copied verbatim; heap-allocate to cover headers + all PDU bytes.
    uint32_t cap = 64u + len;                 // headers + at most the whole input
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
        uint32_t handle   = be_u32(&r);
        uint16_t rnti     = be_u16(&r);
        uint8_t  harq_id  = be_u8(&r);
        uint32_t pdu_len  = be_u32(&r);
        uint8_t  ul_cqi   = be_u8(&r);  (void)ul_cqi;
        uint16_t ta       = be_u16(&r);  (void)ta;
        uint16_t rssi     = be_u16(&r);  (void)rssi;
        if (r.err) break;
        if ((uint64_t)r.off + pdu_len > r.len || pdu_len > OCUDU_RXDATA_MAX_TB_BYTES) {
            SM_Logs(LOG_WARN, _P7_,
                    "[L1->L2 P7] RX_DATA pdu %u len=%u overruns/too big (body=%u); stop.",
                    i, pdu_len, r.len);
            r.err = 1;
            break;
        }
        const uint8_t* tb = r.d + r.off;
        r.off += pdu_len;

        ocudu_wr_u32(&w, handle);
        ocudu_wr_u16(&w, rnti);
        ocudu_wr_u8(&w, harq_id);
        ocudu_wr_u32(&w, pdu_len);            // span length
        if (pdu_len > 0) ocudu_wr_bytes(&w, tb, pdu_len);

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
        rc = ocudu_l2_xsm_put(ctx, OCUDU_FAPI_RX_DATA_INDICATION, body, w.off);
    }

    free(body);
    return rc;
}

#endif /* OAI_OCUDU */
