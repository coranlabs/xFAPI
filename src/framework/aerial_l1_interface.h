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

#ifndef AERIAL_L1_INTERFACE_H
#define AERIAL_L1_INTERFACE_H

struct AppContext;

/* L1 endpoint vtable for AERIAL_OCUDU mode. The Aerial side speaks SCF FAPI
 * over nvIPC shared memory, so init() opens the nvIPC SECONDARY client (toward
 * the Aerial cuphycontroller PRIMARY) AND the xSM handle toward OCUDU-L2
 * (xFAPI owns the memzone in this mode). */
typedef struct AERIAL_L1_Interface {
    int  (*init)(struct AppContext* ctx);
    int  (*send_msg)(struct AppContext* ctx, void* msg);
    void (*destroy)(struct AppContext* ctx);
} AERIAL_L1_Interface;

const AERIAL_L1_Interface* get_aerial_l1_interface(void);

#endif
