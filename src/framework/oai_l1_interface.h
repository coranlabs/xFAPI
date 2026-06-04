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

#ifndef OAI_L1_INTERFACE_H
#define OAI_L1_INTERFACE_H

struct AppContext;

/* L1 endpoint vtable for OAI_OCUDU mode. The OAI side speaks nFAPI over UDP
 * sockets, so init() opens the P5/P7 sockets AND the xSM handle toward
 * OCUDU-L2 (xFAPI owns the memzone in this mode). */
typedef struct OAI_L1_Interface {
    int  (*init)(struct AppContext* ctx);
    int  (*send_msg)(struct AppContext* ctx, void* msg);
    void (*destroy)(struct AppContext* ctx);
} OAI_L1_Interface;

const OAI_L1_Interface* get_oai_l1_interface(void);

#endif
