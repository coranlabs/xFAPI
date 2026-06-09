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

// OAI_OCUDU P5 L2->L1 dispatcher: parse the OCUDU xSM control message and
// route it to the matching OCUDU-FAPI -> nFAPI translator.

#include "oai_l2_to_l1_p5.h"

#ifdef OAI_OCUDU

#include "ocudu_fapi_wire.h"
#include "unified_logger.h"

int ocudu_p5_translate_and_send(struct oai_vnf* v, uint16_t type_id,
                                const void* src_va, uint32_t size)
{
    if (v == NULL || src_va == NULL) {
        return -1;
    }

    uint8_t        msg_type = 0;
    const uint8_t* body     = NULL;
    uint32_t       body_len = 0;
    if (ocudu_xsm_hdr_parse((const uint8_t*)src_va, size,
                            &msg_type, &body, &body_len) != 0) {
        SM_Logs(LOG_ERROR, _P5_,
                "[L2->L1 P5] short/invalid OCUDU msg (type_id=0x%04x size=%u).",
                type_id, size);
        return -1;
    }

    switch (msg_type) {
        case OCUDU_FAPI_PARAM_REQUEST:
            return ocudu_l2l1_param_request(v, body, body_len);
        case OCUDU_FAPI_CONFIG_REQUEST:
            return ocudu_l2l1_config_request(v, body, body_len);
        case OCUDU_FAPI_START_REQUEST:
            return ocudu_l2l1_start_request(v, body, body_len);
        case OCUDU_FAPI_STOP_REQUEST:
            return ocudu_l2l1_stop_request(v, body, body_len);
        default:
            SM_Logs(LOG_WARN, _P5_,
                    "[L2->L1 P5] unhandled OCUDU msg_type=0x%02x (len=%u); "
                    "dropping.", msg_type, body_len);
            return -1;
    }
}

#endif /* OAI_OCUDU */
