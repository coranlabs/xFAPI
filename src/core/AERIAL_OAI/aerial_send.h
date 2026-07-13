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

#ifndef AERIAL_OAI_AERIAL_SEND_H
#define AERIAL_OAI_AERIAL_SEND_H

#ifdef AERIAL_OAI

#include <stdint.h>

struct AppContext;

int aerial_send_p5_msg(struct AppContext* ctx, void* nfapi_p5_hdr,
                       uint32_t msg_len);

int aerial_send_p7_msg(struct AppContext* ctx, void* nfapi_p7_hdr);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_AERIAL_SEND_H */
