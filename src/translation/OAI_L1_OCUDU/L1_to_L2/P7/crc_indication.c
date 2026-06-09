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

// nFAPI CRC.indication (0x86) -> OCUDU CRC_INDICATION.
// tb_crc_status_ok <- (tb_crc_status==0) (OAI pass=0/fail=1); rsrp 0xFFFF=invalid.

#include "oai_l1_to_l2_p7.h"

#ifdef OAI_OCUDU

#include <stdio.h>

#include "../../main/app_context.h"
#include "oai_vnf.h"
#include "oai_l1_to_l2_p5.h"     // ocudu_l2_xsm_put()
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "nfapi_nr_interface.h"  // NFAPI_NR_P7_HEADER_LENGTH
#include "message_stats.h"

// Big-endian cursor over the nFAPI body (bounds-checked).
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

// ul_cqi (cqi=128+2*SNR_dB) -> OCUDU ul_sinr_metric (0.002 dB step) = (cqi-128)*250.
static int16_t cqi_to_ul_sinr_metric(uint8_t ul_cqi)
{
    int32_t m = ((int32_t)ul_cqi - 128) * 250;
    if (m >  32767) m =  32767;
    if (m < -32767) m = -32767;   // -32768 (INT16_MIN) reserved as invalid
    return (int16_t)m;
}

int ocudu_l1l2_crc_indication(struct AppContext* ctx,
                              const uint8_t* nfapi_msg, uint32_t len)
{
    if (nfapi_msg == NULL || len < NFAPI_NR_P7_HEADER_LENGTH + 6u) {
        SM_Logs(LOG_WARN, _P7_, "[L1->L2 P7] CRC.indication too short (%u).", len);
        return -1;
    }

    be_rd_t r = { nfapi_msg + NFAPI_NR_P7_HEADER_LENGTH,
                  len - NFAPI_NR_P7_HEADER_LENGTH, 0, 0 };
    uint16_t sfn   = be_u16(&r);
    uint16_t slot  = be_u16(&r);
    uint16_t n_crc = be_u16(&r);

    oai_vnf_t* v = ctx->oai_ocudu_ctx.vnf;
    int mu = (v != NULL && v->cell_numerology >= 0 && v->cell_numerology < 5)
                 ? v->cell_numerology : 1;
    uint32_t slots_per_frame = 10u << mu;
    uint32_t count = (uint32_t)sfn * slots_per_frame + slot;

    uint8_t    body[4096];
    ocudu_wr_t w;
    ocudu_wr_init(&w, body, sizeof(body));

    ocudu_wr_u8(&w, 1);
    ocudu_wr_u8(&w, (uint8_t)mu);
    ocudu_wr_u32(&w, count);
    ocudu_wr_u16(&w, n_crc);

    char content[MAX_MESSAGE_CONTENT_LEN];
    int  clen = snprintf(content, sizeof(content),
                         "message_type=CRC_INDICATION\nsfn=%u\nslot=%u\nnumber_crcs=%u\n",
                         sfn, slot, n_crc);

    for (uint16_t i = 0; i < n_crc; ++i) {
        uint32_t handle      = be_u32(&r);
        uint16_t rnti        = be_u16(&r);
        uint8_t  harq_id     = be_u8(&r);
        uint8_t  crc_status  = be_u8(&r);
        uint16_t num_cb      = be_u16(&r);
        if (num_cb != 0) {
            // OAI packs (num_cb/8)+1 cb_crc bytes here; not in OCUDU pdu, so skip.
            uint32_t cb_len = (uint32_t)(num_cb / 8u) + 1u;
            if (r.off + cb_len > r.len) r.err = 1;
            else r.off += cb_len;
        }
        uint8_t  ul_cqi      = be_u8(&r);
        uint16_t ta          = be_u16(&r);  (void)ta;
        uint16_t rssi        = be_u16(&r);
        if (r.err) break;

        uint8_t crc_ok = (crc_status == 0) ? 1 : 0;

        ocudu_wr_u32(&w, handle);
        ocudu_wr_u16(&w, rnti);
        ocudu_wr_u8(&w, harq_id);
        ocudu_wr_u8(&w, crc_ok);                       // tb_crc_status_ok (bool)
        ocudu_wr_u16(&w, (uint16_t)cqi_to_ul_sinr_metric(ul_cqi));  // i16 on wire
        ocudu_wr_u8(&w, 0);                            // timing_advance_offset: absent
        ocudu_wr_u16(&w, rssi);                        // rssi (SCF passthrough)
        ocudu_wr_u16(&w, 0xFFFF);                      // rsrp: invalid (OAI has none)

        if (clen >= 0 && clen < (int)sizeof(content))
            clen += snprintf(content + clen, sizeof(content) - clen,
                             "----CRC[%u]----\nhandle=%u\nrnti=%u\nharq_id=%u\n"
                             "tb_crc_status=%u(ok=%u)\nnum_cb=%u\nul_cqi=%u\n"
                             "ul_sinr_metric=%d\nrssi=%u\n",
                             i, handle, rnti, harq_id, crc_status, crc_ok, num_cb,
                             ul_cqi, (int)cqi_to_ul_sinr_metric(ul_cqi), rssi);
    }

    if (r.err || w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] CRC.indication parse/encode error "
                "(rd_off=%u rd_len=%u wr_off=%u n_crc=%u).",
                r.off, r.len, w.off, n_crc);
        return -1;
    }

    add_message_stats("CRC_INDICATION", (int)sfn, (int)slot,
                      (int)w.off, (int)n_crc, content, 0);

    return ocudu_l2_xsm_put(ctx, OCUDU_FAPI_CRC_INDICATION, body, w.off);
}

#endif /* OAI_OCUDU */
