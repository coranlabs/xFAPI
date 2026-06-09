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

// OAI_OCUDU P5 translation, L1 -> L2 direction:
// nFAPI control responses (received from OAI L1) -> OCUDU-FAPI (sent over xSM
// to OCUDU-L2). v1 P5 scope: PARAM.response and CONFIG.response (1-byte
// error_code bodies).

#ifndef OAI_L1_OCUDU_L1_TO_L2_P5_H
#define OAI_L1_OCUDU_L1_TO_L2_P5_H

#ifdef OAI_OCUDU

#include <stdint.h>

struct AppContext;

// Build an OCUDU FAPI message (40-byte fapi_xsm_msg_header + body) and hand it
// to OCUDU-L2 over the L2 xSM (acquire buffer -> memcpy -> Put -> Notify).
// Returns 0 on success, -1 on failure. body may be NULL when body_len == 0.
int ocudu_l2_xsm_put(struct AppContext* ctx, uint8_t msg_type,
                     const void* body, uint32_t body_len);

// Dispatcher called by the VNF P5 handler when a cell-level response arrives
// from OAI. ocudu_msg_type is the OCUDU response type to emit toward L2
// (OCUDU_FAPI_PARAM_RESPONSE / OCUDU_FAPI_CONFIG_RESPONSE).
int ocudu_p5_send_response_to_l2(struct AppContext* ctx,
                                 uint8_t ocudu_msg_type, uint8_t error_code);

// Per-message builders.
int ocudu_l1l2_param_response(struct AppContext* ctx, uint8_t error_code);
int ocudu_l1l2_config_response(struct AppContext* ctx, uint8_t error_code);

#endif /* OAI_OCUDU */
#endif /* OAI_L1_OCUDU_L1_TO_L2_P5_H */
