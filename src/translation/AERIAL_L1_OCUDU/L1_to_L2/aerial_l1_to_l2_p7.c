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

// AERIAL_OCUDU P7 L1->L2 dispatcher: unpack the SCF wire into the matching
// nfapi_nr_*_t and route it to the per-message translator.

#include "aerial_l1_to_l2_p7.h"

#ifdef AERIAL_OCUDU

#include <string.h>

#include "app_context.h"
#include "unified_logger.h"
#include "nr_fapi.h"
#include "nr_fapi_p7.h"
#include "nr_fapi_p7_utils.h"

int aerial_p7_cell_mu(struct AppContext* ctx)
{
    int mu = ctx->aerial_ocudu_ctx.cell_numerology;
    return (mu >= 0 && mu <= 4) ? mu : 1;
}

// Unpack scf_msg into `ind`, translate, then release what the codec allocated.
#define AERIAL_P7_UNPACK_AND_XLATE(type, xlate, release, name)                \
    do {                                                                      \
        type ind;                                                             \
        memset(&ind, 0, sizeof(ind));                                         \
        if (!fapi_nr_p7_message_unpack((void*)scf_msg, msg_len,               \
                                       &ind, sizeof(ind), NULL)) {            \
            SM_Logs(LOG_ERROR, _P7_,                                          \
                    "[L1->L2 P7] " name " unpack failed (len=%u).", msg_len); \
            return -1;                                                        \
        }                                                                     \
        int rc = xlate(ctx, &ind);                                            \
        release(&ind);                                                        \
        return rc;                                                            \
    } while (0)

int aerial_p7_translate_to_l2(struct AppContext* ctx, int32_t msg_id,
                              const uint8_t* scf_msg, uint32_t msg_len,
                              const uint8_t* data_buf, uint32_t data_len)
{
    if (ctx == NULL || scf_msg == NULL) {
        return -1;
    }

    switch (msg_id) {
        case NFAPI_NR_PHY_MSG_TYPE_SLOT_INDICATION:
            return aerial_l1l2_slot_indication(ctx, scf_msg, msg_len);

        case NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION:
            return aerial_l1l2_rx_data_indication(ctx, scf_msg, msg_len,
                                                  data_buf, data_len);

        case NFAPI_NR_PHY_MSG_TYPE_CRC_INDICATION:
            AERIAL_P7_UNPACK_AND_XLATE(nfapi_nr_crc_indication_t,
                                       aerial_l1l2_crc_indication,
                                       free_crc_indication, "CRC.indication");

        case NFAPI_NR_PHY_MSG_TYPE_UCI_INDICATION:
            AERIAL_P7_UNPACK_AND_XLATE(nfapi_nr_uci_indication_t,
                                       aerial_l1l2_uci_indication,
                                       free_uci_indication, "UCI.indication");

        case NFAPI_NR_PHY_MSG_TYPE_SRS_INDICATION:
            AERIAL_P7_UNPACK_AND_XLATE(nfapi_nr_srs_indication_t,
                                       aerial_l1l2_srs_indication,
                                       free_srs_indication, "SRS.indication");

        case NFAPI_NR_PHY_MSG_TYPE_RACH_INDICATION:
            AERIAL_P7_UNPACK_AND_XLATE(nfapi_nr_rach_indication_t,
                                       aerial_l1l2_rach_indication,
                                       free_rach_indication, "RACH.indication");

        // Indications with no translator yet are dropped; never forward raw SCF
        // bytes to OCUDU-L2, which speaks a different FAPI dialect.
        default:
            return 0;
    }
}

#endif /* AERIAL_OCUDU */
