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

// nFAPI RACH.indication -> OCUDU RACH_INDICATION (0x89).
// ra_index <- freq_index; timing_advance_offset MUST be present (else preamble dropped).

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

int ocudu_l1l2_rach_indication(struct AppContext* ctx,
                               const uint8_t* nfapi_msg, uint32_t len)
{
    if (nfapi_msg == NULL || len < NFAPI_NR_P7_HEADER_LENGTH + 5u) {
        SM_Logs(LOG_WARN, _P7_, "[L1->L2 P7] RACH.indication too short (%u).", len);
        return -1;
    }

    be_rd_t r = { nfapi_msg + NFAPI_NR_P7_HEADER_LENGTH,
                  len - NFAPI_NR_P7_HEADER_LENGTH, 0, 0 };
    uint16_t sfn   = be_u16(&r);
    uint16_t slot  = be_u16(&r);
    uint8_t  n_pdu = be_u8(&r);

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
    ocudu_wr_u16(&w, n_pdu);

    char content[MAX_MESSAGE_CONTENT_LEN];
    int  clen = snprintf(content, sizeof(content),
                         "message_type=RACH_INDICATION\n"
                         "sfn=%u\n"
                         "slot=%u\n"
                         "number_of_pdus=%u\n",
                         sfn, slot, n_pdu);

    for (uint8_t i = 0; i < n_pdu; ++i) {
        uint16_t phy_cell_id = be_u16(&r);   // OCUDU pdu has no pci field
        uint8_t symbol_index = be_u8(&r);
        uint8_t slot_index   = be_u8(&r);
        uint8_t freq_index   = be_u8(&r);
        uint8_t avg_rssi     = be_u8(&r);
        uint8_t avg_snr      = be_u8(&r);
        uint8_t n_pre        = be_u8(&r);
        if (r.err) break;

        if (clen >= 0 && clen < (int)sizeof(content))
            clen += snprintf(content + clen, sizeof(content) - clen,
                             "----RACH PDU[%u]----\n"
                             "phy_cell_id=%u\n"
                             "symbol_index=%u\n"
                             "slot_index=%u\n"
                             "freq_index=%u\n"
                             "avg_rssi=%u\n"
                             "avg_snr=%u\n"
                             "num_preamble=%u\n",
                             i, phy_cell_id, symbol_index, slot_index,
                             freq_index, avg_rssi, avg_snr, n_pre);

        ocudu_wr_u32(&w, 0);                 // handle
        ocudu_wr_u8(&w, symbol_index);
        ocudu_wr_u8(&w, slot_index);
        ocudu_wr_u8(&w, freq_index);         // ra_index
        ocudu_wr_u32(&w, (uint32_t)avg_rssi);
        ocudu_wr_u8(&w, avg_snr);
        ocudu_wr_u16(&w, n_pre);

        for (uint8_t p = 0; p < n_pre; ++p) {
            uint8_t  preamble_index = be_u8(&r);
            uint16_t ta             = be_u16(&r);
            uint32_t pwr            = be_u32(&r);
            if (r.err) break;

            if (clen >= 0 && clen < (int)sizeof(content))
                clen += snprintf(content + clen, sizeof(content) - clen,
                                 "  preamble[%u].preamble_index=%u\n"
                                 "  preamble[%u].timing_advance=%u\n"
                                 "  preamble[%u].preamble_pwr=%u\n",
                                 p, preamble_index, p, ta, p, pwr);

            // Tc = ta * 16 * 64 / 2^mu (1024 = 16*64). Invalid (0xffff) -> 0.
            int64_t ta_tc = (ta == 0xffff) ? 0
                            : (((int64_t)ta * 1024) >> mu);

            ocudu_wr_u8(&w, preamble_index);
            ocudu_wr_u8(&w, 1);              // timing_advance_offset present (required)
            ocudu_wr_i64(&w, ta_tc);
            ocudu_wr_u32(&w, pwr);           // preamble_pwr (passed through)
            ocudu_wr_u8(&w, avg_snr);        // OCUDU per-preamble snr (no nFAPI equiv)
        }
    }

    if (r.err || w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] RACH.indication parse/encode error "
                "(rd_off=%u rd_len=%u wr_off=%u n_pdu=%u).",
                r.off, r.len, w.off, n_pdu);
        return -1;
    }

    add_message_stats("RACH_INDICATION", (int)sfn, (int)slot,
                      (int)w.off, (int)n_pdu, content, 0);

    return ocudu_l2_xsm_put(ctx, OCUDU_FAPI_RACH_INDICATION, body, w.off);
}

#endif /* OAI_OCUDU */
