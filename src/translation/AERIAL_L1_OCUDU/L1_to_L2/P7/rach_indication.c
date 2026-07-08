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

// SCF RACH.indication -> OCUDU RACH_INDICATION (0x89).
// ra_index <- freq_index; timing_advance_offset MUST be present, else OCUDU
// drops the preamble.

#include "aerial_l1_to_l2_p7.h"

#ifdef AERIAL_OCUDU

#include <stdio.h>

#include "app_context.h"
#include "aerial_l1_to_l2_p5.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "message_stats.h"

int aerial_l1l2_rach_indication(struct AppContext* ctx,
                                const nfapi_nr_rach_indication_t* ind)
{
    if (ind == NULL || (ind->number_of_pdus > 0 && ind->pdu_list == NULL)) {
        return -1;
    }

    int      mu    = aerial_p7_cell_mu(ctx);
    uint32_t count = (uint32_t)ind->sfn * (10u << mu) + ind->slot;

    uint8_t    body[4096];
    ocudu_wr_t w;
    ocudu_wr_init(&w, body, sizeof(body));

    ocudu_wr_u8(&w, 1);
    ocudu_wr_u8(&w, (uint8_t)mu);
    ocudu_wr_u32(&w, count);
    ocudu_wr_u16(&w, ind->number_of_pdus);

    char content[MAX_MESSAGE_CONTENT_LEN];
    int  clen = snprintf(content, sizeof(content),
                         "message_type=RACH_INDICATION\n"
                         "sfn=%u\n"
                         "slot=%u\n"
                         "number_of_pdus=%u\n",
                         ind->sfn, ind->slot, ind->number_of_pdus);

    for (uint8_t i = 0; i < ind->number_of_pdus; ++i) {
        const nfapi_nr_prach_indication_pdu_t* p = &ind->pdu_list[i];

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
                             i, p->phy_cell_id, p->symbol_index, p->slot_index,
                             p->freq_index, p->avg_rssi, p->avg_snr,
                             p->num_preamble);

        ocudu_wr_u32(&w, 0);                 // handle
        ocudu_wr_u8(&w, p->symbol_index);
        ocudu_wr_u8(&w, p->slot_index);
        ocudu_wr_u8(&w, p->freq_index);      // ra_index
        ocudu_wr_u32(&w, (uint32_t)p->avg_rssi);
        ocudu_wr_u8(&w, p->avg_snr);
        ocudu_wr_u16(&w, p->num_preamble);

        for (uint8_t q = 0; q < p->num_preamble; ++q) {
            const nfapi_nr_prach_indication_preamble_t* pre = &p->preamble_list[q];

            if (clen >= 0 && clen < (int)sizeof(content))
                clen += snprintf(content + clen, sizeof(content) - clen,
                                 "  preamble[%u].preamble_index=%u\n"
                                 "  preamble[%u].timing_advance=%u\n"
                                 "  preamble[%u].preamble_pwr=%u\n",
                                 q, pre->preamble_index, q, pre->timing_advance,
                                 q, pre->preamble_pwr);

            // Tc = ta * 16 * 64 / 2^mu (1024 = 16*64). Invalid (0xffff) -> 0.
            int64_t ta_tc = (pre->timing_advance == 0xffff) ? 0
                            : (((int64_t)pre->timing_advance * 1024) >> mu);

            ocudu_wr_u8(&w, pre->preamble_index);
            ocudu_wr_u8(&w, 1);              // timing_advance_offset present
            ocudu_wr_i64(&w, ta_tc);
            ocudu_wr_u32(&w, pre->preamble_pwr);
            ocudu_wr_u8(&w, p->avg_snr);     // OCUDU per-preamble snr
        }
    }

    if (w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] RACH.indication encode overflow (n_pdu=%u).",
                ind->number_of_pdus);
        return -1;
    }

    add_message_stats("RACH_INDICATION", (int)ind->sfn, (int)ind->slot,
                      (int)w.off, 1, content, 0);

    SM_Logs(LOG_INFO, _P7_,
            "[L1->L2 P7] RACH.indication SFN %u.%u n_pdu=%u -> OCUDU-L2.",
            ind->sfn, ind->slot, ind->number_of_pdus);

    return aerial_l2_xsm_put(ctx, OCUDU_FAPI_RACH_INDICATION, body, w.off);
}

#endif /* AERIAL_OCUDU */
