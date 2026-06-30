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

// AERIAL_OCUDU nvIPC client: xFAPI runs as the nvIPC SECONDARY (the role
// testMAC plays); Aerial cuphycontroller is the nvIPC PRIMARY that owns the
// shared-memory pools. This module owns the secondary attach + the Aerial->L2
// RX loop (epoll on get_fd, or blocking on rx_tti_sem_wait) and the L2->Aerial
// TX helper.

#ifndef AERIAL_OCUDU_AERIAL_NVIPC_H
#define AERIAL_OCUDU_AERIAL_NVIPC_H

#ifdef AERIAL_OCUDU

#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>

#include "nv_ipc.h"

struct AppContext;

// ---------------------------------------------------------------------------
// nvIPC secondary runtime state. Allocated once in aerial_nvipc_start(), owned
// by AppContext (ctx->aerial_ocudu_ctx.nvipc), freed in aerial_nvipc_stop().
// ---------------------------------------------------------------------------
typedef struct aerial_nvipc {
    struct AppContext* ctx;       // back-pointer

    nv_ipc_config_t    cfg;       // config loaded from the nvIPC YAML
    nv_ipc_t*          ipc;       // nvIPC instance (NULL until PRIMARY is up)

    int                blocking;  // 1 = rx_tti_sem_wait loop, 0 = epoll(get_fd)
    int                epoll_fd;  // epoll instance (epoll mode only; -1 otherwise)

    // Attach + RX management thread: retries the secondary attach until the
    // PRIMARY exists, then runs the Aerial->L2 receive loop.
    pthread_t          rx_tid;
    int                rx_started;
    atomic_int         running;   // master run flag for the RX/attach thread

    // Stats.
    atomic_uint        rx_count;  // nvIPC messages received from Aerial
    atomic_uint        tx_count;  // nvIPC messages sent to Aerial
} aerial_nvipc_t;

// ---------------------------------------------------------------------------
// Public API (called from aerial_l1_interface.c / the L2->Aerial forwarder).
// ---------------------------------------------------------------------------

// Allocate + initialize nvIPC state and start the attach/RX thread. The thread
// retries the secondary attach until Aerial (PRIMARY) is up, then receives SCF
// FAPI messages and (eventually) translates them toward OCUDU-L2. Returns 0 on
// success, -1 on failure. Non-blocking: startup does not gate on Aerial.
int  aerial_nvipc_start(struct AppContext* ctx);

// Stop the RX thread, destroy the nvIPC instance, free state. Idempotent.
void aerial_nvipc_stop(struct AppContext* ctx);

// Send one message to Aerial over nvIPC: tx_allocate -> fill msg_buf/data_buf
// -> tx_send_msg -> notify. msg/data may be NULL when their length is 0.
// Returns 0 on success, -1 on failure (e.g. PRIMARY not yet attached).
int  aerial_nvipc_send(struct AppContext* ctx, int32_t msg_id, int32_t cell_id,
                       const void* msg, uint32_t msg_len,
                       const void* data, uint32_t data_len);

// 1 once the secondary has attached to the Aerial PRIMARY (read by the
// L2->Aerial forwarder to gate sends until the link is live).
int  aerial_nvipc_is_attached(struct aerial_nvipc* nv);

#endif /* AERIAL_OCUDU */
#endif /* AERIAL_OCUDU_AERIAL_NVIPC_H */
