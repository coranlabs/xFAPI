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

// AERIAL_OCUDU P5 translation, L2 -> L1 direction:
// OCUDU-FAPI control requests (received over xSM) -> nFAPI (sent to OAI L1).

#ifndef AERIAL_L1_OCUDU_L2_TO_L1_P5_H
#define AERIAL_L1_OCUDU_L2_TO_L1_P5_H

#ifdef AERIAL_OCUDU

#include <stdint.h>

struct AppContext;

// Dispatcher called by the L2->Aerial forwarder. src_va/size is the full xSM
// buffer from OCUDU-L2 (40-byte fapi_xsm_msg_header + body); type_id is the
// OCUDU msg_type. Parses the header, routes to the per-message translator,
// which builds the equivalent nfapi_nr_*_scf_t message; aerial_send_p5_msg()
// then packs it to SCF FAPI and sends it to Aerial over nvIPC.
// Returns 0 on success, -1 on error (unknown/short message or send failure).
int aerial_p5_translate_and_send(struct AppContext* ctx, uint16_t type_id,
                                const void* src_va, uint32_t size);

// Per-message translators. body points past the 40-byte xSM header,
// body_len is the OCUDU-declared body length.
int aerial_l2l1_param_request(struct AppContext* ctx,
                             const uint8_t* body, uint32_t body_len);
int aerial_l2l1_config_request(struct AppContext* ctx,
                              const uint8_t* body, uint32_t body_len);
int aerial_l2l1_start_request(struct AppContext* ctx,
                             const uint8_t* body, uint32_t body_len);
int aerial_l2l1_stop_request(struct AppContext* ctx,
                            const uint8_t* body, uint32_t body_len);

#endif /* AERIAL_OCUDU */
#endif /* AERIAL_L1_OCUDU_L2_TO_L1_P5_H */
