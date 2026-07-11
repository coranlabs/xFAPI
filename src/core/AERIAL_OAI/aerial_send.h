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

// AERIAL_OAI OAI->Aerial send path: pack an nFAPI message struct into the SCF
// FAPI wire (little-endian codec) and hand it to the nvIPC client.

#ifndef AERIAL_OAI_AERIAL_SEND_H
#define AERIAL_OAI_AERIAL_SEND_H

#ifdef AERIAL_OAI

#include <stdint.h>

struct AppContext;

// Pack + send a P5 message. nfapi_p5_hdr points at an nfapi_nr_p4_p5_message_
// header_t-prefixed struct; msg_len is its in-memory size.
int aerial_send_p5_msg(struct AppContext* ctx, void* nfapi_p5_hdr,
                       uint32_t msg_len);

// Pack + send a P7 message. nfapi_p7_hdr points at an nfapi_nr_p7_message_
// header_t-prefixed struct. TX_DATA transport blocks are packed into data_buf.
int aerial_send_p7_msg(struct AppContext* ctx, void* nfapi_p7_hdr);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_AERIAL_SEND_H */
