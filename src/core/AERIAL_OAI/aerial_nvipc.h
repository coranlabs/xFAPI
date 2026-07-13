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

#ifndef AERIAL_OAI_AERIAL_NVIPC_H
#define AERIAL_OAI_AERIAL_NVIPC_H

#ifdef AERIAL_OAI

#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>

#include "nv_ipc.h"

struct AppContext;

typedef struct aerial_nvipc {
    struct AppContext* ctx;

    nv_ipc_config_t    cfg;
    nv_ipc_t*          ipc;

    int                blocking;
    int                epoll_fd;

    pthread_t          rx_tid;
    int                rx_started;
    atomic_int         running;

    atomic_uint        rx_count;
    atomic_uint        tx_count;
} aerial_nvipc_t;

int  aerial_nvipc_start(struct AppContext* ctx);

void aerial_nvipc_stop(struct AppContext* ctx);

int  aerial_nvipc_send(struct AppContext* ctx, int32_t msg_id, int32_t cell_id,
                       const void* msg, uint32_t msg_len,
                       const void* data, uint32_t data_len);

int  aerial_nvipc_is_attached(struct aerial_nvipc* nv);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_AERIAL_NVIPC_H */
