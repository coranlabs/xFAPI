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

// Re-frame bridge: Aerial speaks little-endian SCF FAPI, OAI speaks big-endian
// nFAPI; both encode the same open-nFAPI structs, so a message is bridged by
// unpacking with one codec and packing with the other.

#ifndef AERIAL_OAI_BRIDGE_H
#define AERIAL_OAI_BRIDGE_H

#ifdef AERIAL_OAI

#include <stdint.h>

struct AppContext;
struct oai_pnf;

int aerial_oai_bridge_p5_to_oai(struct AppContext* ctx, int32_t msg_id,
                                const uint8_t* scf_msg, uint32_t scf_len);

int aerial_oai_bridge_p7_to_oai(struct AppContext* ctx, int32_t msg_id,
                                const uint8_t* scf_msg, uint32_t scf_len,
                                const uint8_t* data_buf, uint32_t data_len);

int aerial_oai_bridge_p5_to_aerial(struct AppContext* ctx,
                                   const uint8_t* nfapi_msg, uint32_t len);

int aerial_oai_bridge_p7_to_aerial(struct AppContext* ctx,
                                   const uint8_t* nfapi_msg, uint32_t len);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_BRIDGE_H */
