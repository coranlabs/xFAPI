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

#ifndef AERIAL_OAI_L1_INTERFACE_H
#define AERIAL_OAI_L1_INTERFACE_H

struct AppContext;

/* L1 endpoint vtable for AERIAL_OAI mode. The Aerial side speaks SCF FAPI over
 * nvIPC shared memory; init() opens the nvIPC SECONDARY client toward the Aerial
 * cuphycontroller PRIMARY. Unlike AERIAL_OCUDU there is no xSM handle here — the
 * L2 side is OAI over sockets, owned by the OAI L2 interface. */
typedef struct AERIAL_OAI_L1_Interface {
    int  (*init)(struct AppContext* ctx);
    int  (*send_msg)(struct AppContext* ctx, void* msg);
    void (*destroy)(struct AppContext* ctx);
} AERIAL_OAI_L1_Interface;

const AERIAL_OAI_L1_Interface* get_aerial_oai_l1_interface(void);

#endif
