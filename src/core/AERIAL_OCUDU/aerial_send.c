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

#include "aerial_send.h"

#ifdef AERIAL_OCUDU

#include <string.h>

#include "../../main/app_context.h"
#include "aerial_nvipc.h"
#include "unified_logger.h"

#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"
#include "nr_fapi.h"

// Aerial's cpu_msg pool buffer is 15000 B; a packed P5 message never exceeds it.
#define AERIAL_P5_TX_BUF_SIZE 15000u

int aerial_send_p5_msg(struct AppContext* ctx, void* nfapi_p5_hdr,
                       uint32_t msg_len)
{
    if (ctx == NULL || nfapi_p5_hdr == NULL) {
        return -1;
    }
    nfapi_nr_p4_p5_message_header_t* hdr =
        (nfapi_nr_p4_p5_message_header_t*)nfapi_p5_hdr;

    uint8_t packed[AERIAL_P5_TX_BUF_SIZE];
    int packed_len = fapi_nr_p5_message_pack(nfapi_p5_hdr, msg_len,
                                             packed, sizeof(packed), NULL);
    if (packed_len <= 0) {
        SM_Logs(LOG_ERROR, _P5_,
                "[L2->L1 P5] SCF pack failed for msg_id=0x%02x (rc=%d).",
                hdr->message_id, packed_len);
        return -1;
    }

    SM_Logs(LOG_INFO, _P5_,
            "[L2->L1 P5] msg_id=0x%02x -> Aerial (msg_len=%d).",
            hdr->message_id, packed_len);

    // fapi_nr_p5_message_pack() already counts the 8-byte SCF prologue in its
    // return value, so msg_len is exactly packed_len. (OAI's aerial adapter
    // adds a further 8 here, which is why cuphycontroller logs an 8-byte
    // "Incorrect msg length" warning for every OAI P5 message.)
    return aerial_nvipc_send(ctx, (int32_t)hdr->message_id,
                             ctx->aerial_ocudu_ctx.cell_id,
                             packed, (uint32_t)packed_len, NULL, 0);
}

#endif /* AERIAL_OCUDU */
