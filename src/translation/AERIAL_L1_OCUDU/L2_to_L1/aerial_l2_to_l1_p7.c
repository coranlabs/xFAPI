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

// AERIAL_OCUDU P7 L2->L1 dispatcher: parse the OCUDU xSM data-plane message and
// route it to the matching OCUDU-FAPI -> nFAPI translator.

#include "aerial_l2_to_l1_p7.h"

#ifdef AERIAL_OCUDU

#include "ocudu_fapi_wire.h"
#include "unified_logger.h"

int aerial_p7_translate_and_send(struct AppContext* ctx,
                                uint16_t type_id, const void* src_va,
                                uint32_t size)
{
    if (ctx == NULL || src_va == NULL) {
        return -1;
    }

    // Aerial has no end-of-slot marker
    if (type_id == OCUDU_FAPI_P7_LAST_MESSAGE) {
        return 0;
    }

    uint8_t        msg_type = 0;
    const uint8_t* body     = NULL;
    uint32_t       body_len = 0;
    if (ocudu_xsm_hdr_parse((const uint8_t*)src_va, size,
                            &msg_type, &body, &body_len) != 0) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L2->L1 P7] short/invalid OCUDU msg (type_id=0x%04x size=%u).",
                type_id, size);
        return -1;
    }

    switch (msg_type) {
        case OCUDU_FAPI_DL_TTI_REQUEST:
            return aerial_l2l1_dl_tti_request(ctx, body, body_len);

        case OCUDU_FAPI_TX_DATA_REQUEST:
            return aerial_l2l1_tx_data_request(ctx, body, body_len);

        case OCUDU_FAPI_UL_TTI_REQUEST:
            return aerial_l2l1_ul_tti_request(ctx, body, body_len);

        case OCUDU_FAPI_UL_DCI_REQUEST:
            return aerial_l2l1_ul_dci_request(ctx, body, body_len);

        // End-of-slot sentinel (P7_LAST_MESSAGE) and any untranslated msg_type are
        // dropped; never forward raw OCUDU-FAPI bytes to OAI (different P7 dialect).
        default:
            return 0;
    }
}

#endif /* AERIAL_OCUDU */
