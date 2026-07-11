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

#ifndef OAI_L2_INTERFACE_H
#define OAI_L2_INTERFACE_H

struct AppContext;

/* L2 endpoint vtable for AERIAL_OAI mode. The OAI side is the MAC, which speaks
 * classic nFAPI (SCTP P5 + UDP P7) as the VNF. xFAPI presents the PNF (server)
 * face: init() opens the SCTP listener + UDP socket and starts the PNF handshake
 * responder and the OAI->Aerial forwarder. */
typedef struct OAI_L2_Interface {
    int  (*init)(struct AppContext* ctx);
    int  (*send_msg)(struct AppContext* ctx, void* msg);
    void (*destroy)(struct AppContext* ctx);
} OAI_L2_Interface;

const OAI_L2_Interface* get_oai_l2_interface(void);

#endif
