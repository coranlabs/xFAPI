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

// AERIAL_OCUDU P5 translation, L1 -> L2: SCF FAPI control responses (nvIPC)
// -> OCUDU-FAPI (xSM to OCUDU-L2).

#ifndef AERIAL_L1_OCUDU_L1_TO_L2_P5_H
#define AERIAL_L1_OCUDU_L1_TO_L2_P5_H

#ifdef AERIAL_OCUDU

#include <stdint.h>

struct AppContext;

// Build an OCUDU FAPI message (40-byte xSM header + body) and Put it to L2.
int aerial_l2_xsm_put(struct AppContext* ctx, uint8_t msg_type,
                     const void* body, uint32_t body_len);

int aerial_p5_send_response_to_l2(struct AppContext* ctx,
                                 uint8_t ocudu_msg_type, uint8_t error_code);

int aerial_l1l2_param_response(struct AppContext* ctx, uint8_t error_code);
int aerial_l1l2_config_response(struct AppContext* ctx, uint8_t error_code);

#endif /* AERIAL_OCUDU */
#endif /* AERIAL_L1_OCUDU_L1_TO_L2_P5_H */
