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

// AERIAL_OCUDU P7 translation, L1 -> L2 direction: SCF FAPI indications
// (received from Aerial over nvIPC) -> OCUDU-FAPI (sent over xSM to OCUDU-L2).

#ifndef AERIAL_L1_OCUDU_L1_TO_L2_P7_H
#define AERIAL_L1_OCUDU_L1_TO_L2_P7_H

#ifdef AERIAL_OCUDU

#include <stdint.h>

struct AppContext;

// Fixed SCF prologue: scf_fapi_header_t (message_count u8, handle_id u8) +
// scf_fapi_body_header_t (type_id u16, length u32). The body starts here.
#define AERIAL_SCF_MSG_HDR_SIZE 8u

// scf_msg/msg_len is the whole nv_ipc_msg_t.msg_buf as received from Aerial.
int aerial_l1l2_slot_indication(struct AppContext* ctx,
                                const uint8_t* scf_msg, uint32_t msg_len);

#endif /* AERIAL_OCUDU */
#endif /* AERIAL_L1_OCUDU_L1_TO_L2_P7_H */
