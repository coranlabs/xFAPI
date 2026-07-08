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

// SCF CRC.indication -> OCUDU CRC_INDICATION (0x86).
// tb_crc_status_ok <- (tb_crc_status == 0); rsrp 0xFFFF = invalid.
// timing_advance_offset is emitted absent: SCF carries TA relative to 31
// (NTA_new = NTA_old + (TA-31)*16*64/2^mu), OCUDU expects an absolute offset.

#include "aerial_l1_to_l2_p7.h"

#ifdef AERIAL_OCUDU

#include <stdio.h>

#include "app_context.h"
#include "aerial_l1_to_l2_p5.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "message_stats.h"

int aerial_l1l2_crc_indication(struct AppContext* ctx,
                               const nfapi_nr_crc_indication_t* ind)
{
    if (ind == NULL || (ind->number_crcs > 0 && ind->crc_list == NULL)) {
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
    ocudu_wr_u16(&w, ind->number_crcs);

    char content[MAX_MESSAGE_CONTENT_LEN];
    int  clen = snprintf(content, sizeof(content),
                         "message_type=CRC_INDICATION\nsfn=%u\nslot=%u\nnumber_crcs=%u\n",
                         ind->sfn, ind->slot, ind->number_crcs);

    for (uint16_t i = 0; i < ind->number_crcs; ++i) {
        const nfapi_nr_crc_t* c = &ind->crc_list[i];
        uint8_t crc_ok = (c->tb_crc_status == 0) ? 1 : 0;

        ocudu_wr_u32(&w, c->handle);
        ocudu_wr_u16(&w, c->rnti);
        ocudu_wr_u8(&w, c->harq_id);
        ocudu_wr_u8(&w, crc_ok);
        ocudu_wr_u16(&w, (uint16_t)aerial_cqi_to_ul_sinr_metric(c->ul_cqi));
        ocudu_wr_u8(&w, 0);
        ocudu_wr_u16(&w, c->rssi);
        ocudu_wr_u16(&w, 0xFFFF);

        if (clen >= 0 && clen < (int)sizeof(content))
            clen += snprintf(content + clen, sizeof(content) - clen,
                             "----CRC[%u]----\nhandle=%u\nrnti=%u\nharq_id=%u\n"
                             "tb_crc_status=%u(ok=%u)\nnum_cb=%u\nul_cqi=%u\n"
                             "ul_sinr_metric=%d\nrssi=%u\n",
                             i, c->handle, c->rnti, c->harq_id, c->tb_crc_status,
                             crc_ok, c->num_cb, c->ul_cqi,
                             (int)aerial_cqi_to_ul_sinr_metric(c->ul_cqi), c->rssi);
    }

    if (w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] CRC.indication encode overflow (n_crc=%u).",
                ind->number_crcs);
        return -1;
    }

    add_message_stats("CRC_INDICATION", (int)ind->sfn, (int)ind->slot,
                      (int)w.off, (int)ind->number_crcs, content, 0);

    return aerial_l2_xsm_put(ctx, OCUDU_FAPI_CRC_INDICATION, body, w.off);
}

#endif /* AERIAL_OCUDU */
