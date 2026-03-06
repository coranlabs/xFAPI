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

#ifndef OCUDU_L1_INTERFACE_H
#define OCUDU_L1_INTERFACE_H

struct AppContext;

typedef struct OCUDU_L1_Interface {
    int  (*init)(struct AppContext* ctx);
    int  (*send_msg)(struct AppContext* ctx, void* msg);
    void (*destroy)(struct AppContext* ctx);
} OCUDU_L1_Interface;

const OCUDU_L1_Interface* get_ocudu_l1_interface(void);

#endif
