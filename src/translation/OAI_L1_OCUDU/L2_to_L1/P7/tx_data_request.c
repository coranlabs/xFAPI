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

// OAI_OCUDU P7 L2->L1: TX_DATA.request (OCUDU-FAPI -> nFAPI -> PNF). Each PDU's
// transport block becomes one nfapi_nr_pdu_t with a single inline TLV (tag=0).

#include "oai_l2_to_l1_p7.h"

#ifdef OAI_OCUDU

#include <stdlib.h>
#include <string.h>

#include "ocudu_fapi_wire.h"
#include "oai_vnf.h"
#include "unified_logger.h"
#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"
#include "message_stats.h"

// Max transport-block bytes we will inline into a TLV (nFAPI TLV direct[] holds
// 152064 bytes; far more than any single TB here).
#define OCUDU_TXDATA_MAX_TB_BYTES 152064u

int ocudu_l2l1_tx_data_request(struct AppContext* ctx, struct oai_vnf* v,
                               const uint8_t* body, uint32_t body_len)
{
    ocudu_rd_t r;
    ocudu_rd_init(&r, body, body_len);

    uint8_t valid = ocudu_rd_u8(&r);
    if (!valid) {
        SM_Logs(LOG_WARN, _P7_, "[L2->L1 P7] TX_DATA invalid slot_point; dropping.");
        return 0;
    }
    uint8_t  mu    = ocudu_rd_u8(&r);
    uint32_t count = ocudu_rd_u32(&r);
    uint16_t n_pdus = ocudu_rd_u16(&r);
    if (r.error) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] TX_DATA header underrun; dropping.");
        return -1;
    }

    unsigned slots_per_frame = 10u << mu;
    uint16_t sfn  = (uint16_t)((count / slots_per_frame) % 1024u);
    uint16_t slot = (uint16_t)(count % slots_per_frame);

    nfapi_nr_tx_data_request_t* tx =
        (nfapi_nr_tx_data_request_t*)calloc(1, sizeof(*tx));
    if (tx == NULL) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] TX_DATA calloc(%zu) failed.",
                sizeof(*tx));
        return -1;
    }

    tx->header.phy_id     = (uint16_t)v->phy_id;
    tx->header.message_id = NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST;
    tx->SFN  = sfn;
    tx->Slot = slot;

    unsigned out = 0;
    for (uint16_t i = 0; i < n_pdus && !r.error; ++i) {
        uint16_t pdu_index = ocudu_rd_u16(&r);
        (void)ocudu_rd_u8(&r);               // cw_index (consume; single-CW TBs)
        uint32_t tb_size   = ocudu_rd_u32(&r);
        if (r.error || (uint64_t)r.off + tb_size > body_len) {
            SM_Logs(LOG_WARN, _P7_,
                    "[L2->L1 P7] TX_DATA pdu %u tb_size=%u overruns body=%u; stop.",
                    i, tb_size, body_len);
            r.error = 1;
            break;
        }
        const uint8_t* tb = r.data + r.off;
        ocudu_rd_skip(&r, tb_size);

        if (out < NFAPI_NR_MAX_TX_REQUEST_PDUS && tb_size <= OCUDU_TXDATA_MAX_TB_BYTES) {
            nfapi_nr_pdu_t* p = &tx->pdu_list[out];
            p->PDU_length = tb_size;
            p->PDU_index  = pdu_index;
            p->num_TLV    = 1;
            p->TLVs[0].tag    = 0;          // inline payload in value.direct
            p->TLVs[0].length = tb_size;
            if (tb_size > 0) {
                memcpy(p->TLVs[0].value.direct, tb, tb_size);
            }
            out++;
        }
    }

    int walk_ok = (!r.error && r.off == body_len);
    if (!walk_ok) {
        SM_Logs(LOG_WARN, _P7_,
                "[L2->L1 P7] TX_DATA slot=%u.%u walk desync "
                "(consumed=%u body=%u err=%d n_pdus=%u) -> dropping.",
                sfn, slot, r.off, body_len, r.error, n_pdus);
        free(tx);
        return -1;
    }

    tx->Number_of_PDUs = (uint16_t)out;

    // Record the translated nFAPI message for the XFAPI dashboard.
    {
        char content[MAX_MESSAGE_CONTENT_LEN];
        content[0] = '\0';
        serialize_nfapi_tx_data_request_message(content, sizeof(content), tx);
        add_message_stats("TX_DATA_REQUEST", tx->SFN, tx->Slot,
                          (int)sizeof(*tx), tx->Number_of_PDUs, content, 0);
    }

    int rc = oai_vnf_send_p7(ctx, (nfapi_p7_message_header_t*)&tx->header);
    free(tx);
    return (rc == 0) ? 1 : -1;
}

#endif /* OAI_OCUDU */
