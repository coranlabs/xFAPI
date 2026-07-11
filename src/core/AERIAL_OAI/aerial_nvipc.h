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

// AERIAL_OAI nvIPC client: xFAPI runs as the nvIPC SECONDARY (the role the MAC
// plays); Aerial cuphycontroller is the nvIPC PRIMARY that owns the SHM pools.
// This module owns the secondary attach + the Aerial->OAI RX loop (epoll on
// get_fd, or blocking on rx_tti_sem_wait) and the OAI->Aerial TX helper.

#ifndef AERIAL_OAI_AERIAL_NVIPC_H
#define AERIAL_OAI_AERIAL_NVIPC_H

#ifdef AERIAL_OAI

#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>

#include "nv_ipc.h"

struct AppContext;

typedef struct aerial_nvipc {
    struct AppContext* ctx;       // back-pointer

    nv_ipc_config_t    cfg;       // secondary SHM config (prefix only)
    nv_ipc_t*          ipc;       // nvIPC instance (NULL until PRIMARY is up)

    int                blocking;  // 1 = rx_tti_sem_wait loop, 0 = epoll(get_fd)
    int                epoll_fd;  // epoll instance (epoll mode only; -1 otherwise)

    pthread_t          rx_tid;    // attach + Aerial->OAI RX thread
    int                rx_started;
    atomic_int         running;

    atomic_uint        rx_count;  // nvIPC messages received from Aerial
    atomic_uint        tx_count;  // nvIPC messages sent to Aerial
} aerial_nvipc_t;

// Allocate + initialize nvIPC state and start the attach/RX thread. Non-blocking:
// startup does not gate on Aerial. Returns 0 on success, -1 on failure.
int  aerial_nvipc_start(struct AppContext* ctx);

// Stop the RX thread, destroy the nvIPC instance, free state. Idempotent.
void aerial_nvipc_stop(struct AppContext* ctx);

// Send one message to Aerial over nvIPC: tx_allocate -> fill msg_buf/data_buf
// -> tx_send_msg -> notify. msg/data may be NULL when their length is 0.
int  aerial_nvipc_send(struct AppContext* ctx, int32_t msg_id, int32_t cell_id,
                       const void* msg, uint32_t msg_len,
                       const void* data, uint32_t data_len);

// 1 once the secondary has attached to the Aerial PRIMARY.
int  aerial_nvipc_is_attached(struct aerial_nvipc* nv);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_AERIAL_NVIPC_H */
