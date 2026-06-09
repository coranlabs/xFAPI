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

// OAI_OCUDU P7 L1->L2 dispatcher: route a reassembled nFAPI uplink/indication
// message to the matching nFAPI -> OCUDU-FAPI translator.

#include "oai_l1_to_l2_p7.h"

#ifdef OAI_OCUDU

#include "unified_logger.h"
#include "nfapi_nr_interface_scf.h"

int ocudu_p7_translate_to_l2(struct AppContext* ctx, uint16_t nfapi_msg_id,
                             const uint8_t* nfapi_msg, uint32_t len)
{
    switch (nfapi_msg_id) {
        case NFAPI_NR_PHY_MSG_TYPE_SLOT_INDICATION:
            return ocudu_l1l2_slot_indication(ctx, nfapi_msg, len);

        case NFAPI_NR_PHY_MSG_TYPE_RACH_INDICATION:
            return ocudu_l1l2_rach_indication(ctx, nfapi_msg, len);

        case NFAPI_NR_PHY_MSG_TYPE_CRC_INDICATION:
            return ocudu_l1l2_crc_indication(ctx, nfapi_msg, len);

        case NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION:
            return ocudu_l1l2_rx_data_indication(ctx, nfapi_msg, len);

        case NFAPI_NR_PHY_MSG_TYPE_UCI_INDICATION:
            return ocudu_l1l2_uci_indication(ctx, nfapi_msg, len);

        // Indications with no translator yet (e.g. SRS) are dropped; never forward
        // raw nFAPI bytes to OCUDU-L2, which speaks a different FAPI dialect.
        default:
            return 0;
    }
}

#endif /* OAI_OCUDU */
