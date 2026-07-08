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

// SCF SRS.indication -> OCUDU SRS_INDICATION (0x88).
// Only the control portion is translated: the channel matrix and positioning
// reports ride in nv_ipc_msg_t.data_buf in a cuPHY-specific layout, so both
// optionals are emitted absent and OCUDU-L2 gets handle/rnti/timing advance.

#include "aerial_l1_to_l2_p7.h"

#ifdef AERIAL_OCUDU

#include <stdio.h>

#include "app_context.h"
#include "aerial_l1_to_l2_p5.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "message_stats.h"

int aerial_l1l2_srs_indication(struct AppContext* ctx,
                               const nfapi_nr_srs_indication_t* ind)
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
                         "message_type=SRS_INDICATION\nsfn=%u\nslot=%u\nnumber_of_pdus=%u\n",
                         ind->sfn, ind->slot, ind->number_of_pdus);

    for (uint8_t i = 0; i < ind->number_of_pdus; ++i) {
        const nfapi_nr_srs_indication_pdu_t* p = &ind->pdu_list[i];

        ocudu_wr_u32(&w, p->handle);
        ocudu_wr_u16(&w, p->rnti);

        // SRS timing_advance_offset is absolute, in units of 16*64*Tc/2^mu.
        if (p->timing_advance_offset == 0xffff) {
            ocudu_wr_u8(&w, 0);
        } else {
            ocudu_wr_u8(&w, 1);
            ocudu_wr_i64(&w, ((int64_t)p->timing_advance_offset * 1024) >> mu);
        }

        ocudu_wr_u8(&w, 0);   // matrix: absent
        ocudu_wr_u8(&w, 0);   // positioning: absent

        if (clen >= 0 && clen < (int)sizeof(content))
            clen += snprintf(content + clen, sizeof(content) - clen,
                             "----SRS PDU[%u]----\nhandle=%u\nrnti=%u\n"
                             "timing_advance_offset=%u\nsrs_usage=%u\nreport_type=%u\n",
                             i, p->handle, p->rnti, p->timing_advance_offset,
                             p->srs_usage, p->report_type);
    }

    if (w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] SRS.indication encode overflow (n_pdu=%u).",
                ind->number_of_pdus);
        return -1;
    }

    add_message_stats("SRS_INDICATION", (int)ind->sfn, (int)ind->slot,
                      (int)w.off, (int)ind->number_of_pdus, content, 0);

    return aerial_l2_xsm_put(ctx, OCUDU_FAPI_SRS_INDICATION, body, w.off);
}

#endif /* AERIAL_OCUDU */
