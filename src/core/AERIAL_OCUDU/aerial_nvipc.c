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

// AERIAL_OCUDU nvIPC secondary client. Scaffold milestone: brings up the
// secondary attach toward the Aerial PRIMARY and drains the RX queue, logging
// each SCF FAPI message. SCF-FAPI <-> OCUDU-FAPI translation is layered on in
// later milestones (see src/translation/AERIAL_L1_OCUDU/).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "aerial_nvipc.h"

#ifdef AERIAL_OCUDU

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>

#include "../../main/app_context.h"
#include "unified_logger.h"

#define AERIAL_NVIPC_EPOLL_MAX_EVENTS   8
#define AERIAL_NVIPC_EPOLL_TIMEOUT_MS   1000
#define AERIAL_NVIPC_ATTACH_POLL_MS     200
#define AERIAL_NVIPC_ATTACH_LOG_EVERY_MS 5000

// ---------------------------------------------------------------------------
// Config: build the secondary nv_ipc_config_t from xFAPI's config.
// ---------------------------------------------------------------------------
static int aerial_nvipc_build_config(AppContext* ctx, aerial_nvipc_t* nv)
{
    const nvipc_config_t* c = &ctx->config.nvipc;
    memset(&nv->cfg, 0, sizeof(nv->cfg));

    // A SHM secondary only needs transport=SHM + prefix; it auto-discovers the
    // primary's pool layout at attach time. Build the config inline from
    // xFAPI's own config (no separate nvIPC YAML).
    if (set_nv_ipc_default_config(&nv->cfg, NV_IPC_MODULE_SECONDARY) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[AERIAL_OCUDU] set_nv_ipc_default_config failed.");
        return -1;
    }
    nv->cfg.ipc_transport = NV_IPC_TRANSPORT_SHM;
    strncpy(nv->cfg.transport_config.shm.prefix, c->prefix,
            sizeof(nv->cfg.transport_config.shm.prefix) - 1);
    nv->cfg.transport_config.shm.prefix[
        sizeof(nv->cfg.transport_config.shm.prefix) - 1] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Attach: retry create_nv_ipc_interface() until the Aerial PRIMARY exists.
// Returns 0 once attached, -1 if the run flag was cleared while waiting.
// ---------------------------------------------------------------------------
static int aerial_nvipc_attach(aerial_nvipc_t* nv)
{
    uint32_t waited_ms = 0;
    uint32_t since_log_ms = 0;

    while (atomic_load(&nv->running)) {
        nv->ipc = create_nv_ipc_interface(&nv->cfg);
        if (nv->ipc != NULL) {
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[AERIAL_OCUDU] nvIPC secondary attached to PRIMARY "
                    "(prefix='%s') after ~%us.",
                    nv->cfg.transport_config.shm.prefix, waited_ms / 1000);
            return 0;
        }

        struct timespec ts = { .tv_sec = 0,
                               .tv_nsec = (long)AERIAL_NVIPC_ATTACH_POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
        waited_ms += AERIAL_NVIPC_ATTACH_POLL_MS;
        since_log_ms += AERIAL_NVIPC_ATTACH_POLL_MS;
        if (since_log_ms >= AERIAL_NVIPC_ATTACH_LOG_EVERY_MS) {
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[AERIAL_OCUDU] waiting for Aerial nvIPC PRIMARY "
                    "(prefix='%s', %us elapsed)…",
                    nv->cfg.transport_config.shm.prefix, waited_ms / 1000);
            since_log_ms = 0;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// RX: handle one received nvIPC message. Scaffold: log + (later) translate.
// ---------------------------------------------------------------------------
static void aerial_nvipc_handle_rx(aerial_nvipc_t* nv, const nv_ipc_msg_t* msg)
{
    atomic_fetch_add(&nv->rx_count, 1u);
    SM_Logs(LOG_DEBUG, _P7_,
            "[AERIAL_OCUDU Aerial->L2] nvIPC rx msg_id=0x%02x cell=%d "
            "msg_len=%d data_len=%d (translation TODO)",
            msg->msg_id, msg->cell_id, msg->msg_len, msg->data_len);
    // TODO(milestone 2+): translate SCF FAPI -> OCUDU-FAPI and push into the
    // OCUDU-L2 xSM queue.
}

// Drain all currently-available nvIPC RX messages.
static void aerial_nvipc_drain(aerial_nvipc_t* nv)
{
    nv_ipc_t* ipc = nv->ipc;
    nv_ipc_msg_t msg;
    while (1) {
        memset(&msg, 0, sizeof(msg));
        if (ipc->rx_recv_msg(ipc, &msg) < 0) {
            break;  // no more messages
        }
        aerial_nvipc_handle_rx(nv, &msg);
        ipc->rx_release(ipc, &msg);
    }
}

// ---------------------------------------------------------------------------
// RX loop variants.
// ---------------------------------------------------------------------------
static void aerial_nvipc_rx_loop_epoll(aerial_nvipc_t* nv)
{
    nv_ipc_t* ipc = nv->ipc;

    nv->epoll_fd = epoll_create1(0);
    if (nv->epoll_fd < 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[AERIAL_OCUDU] epoll_create1 failed: %s", strerror(errno));
        return;
    }

    int rx_fd = ipc->get_fd(ipc);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = rx_fd };
    if (epoll_ctl(nv->epoll_fd, EPOLL_CTL_ADD, rx_fd, &ev) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[AERIAL_OCUDU] epoll_ctl(ADD nvipc fd=%d) failed: %s",
                rx_fd, strerror(errno));
        return;
    }

    struct epoll_event events[AERIAL_NVIPC_EPOLL_MAX_EVENTS];
    while (atomic_load(&nv->running)) {
        int nfds = epoll_wait(nv->epoll_fd, events,
                              AERIAL_NVIPC_EPOLL_MAX_EVENTS,
                              AERIAL_NVIPC_EPOLL_TIMEOUT_MS);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            SM_Logs(LOG_ERROR, _XFAPI_,
                    "[AERIAL_OCUDU] epoll_wait failed: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == rx_fd) {
                ipc->get_value(ipc);   // read + clear the eventfd
                aerial_nvipc_drain(nv);
            }
        }
    }
}

static void aerial_nvipc_rx_loop_blocking(aerial_nvipc_t* nv)
{
    nv_ipc_t* ipc = nv->ipc;
    while (atomic_load(&nv->running)) {
        // NOTE: rx_tti_sem_wait blocks until the PRIMARY posts; on shutdown the
        // join may lag one TTI. epoll mode (default) shuts down promptly.
        if (ipc->rx_tti_sem_wait(ipc) < 0) {
            continue;
        }
        aerial_nvipc_drain(nv);
    }
}

// ---------------------------------------------------------------------------
// Attach + RX management thread.
// ---------------------------------------------------------------------------
static void* aerial_nvipc_rx_thread(void* arg)
{
    aerial_nvipc_t* nv = (aerial_nvipc_t*)arg;

    if (aerial_nvipc_attach(nv) != 0) {
        SM_Logs(LOG_WARN, _XFAPI_,
                "[AERIAL_OCUDU] nvIPC attach aborted (shutting down).");
        return NULL;
    }

    if (nv->blocking) {
        aerial_nvipc_rx_loop_blocking(nv);
    } else {
        aerial_nvipc_rx_loop_epoll(nv);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
int aerial_nvipc_start(AppContext* ctx)
{
    if (ctx == NULL) return -1;
    AERIALOCUDUContext* oc = &ctx->aerial_ocudu_ctx;

    aerial_nvipc_t* nv = calloc(1, sizeof(*nv));
    if (nv == NULL) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[AERIAL_OCUDU] nvIPC state alloc failed.");
        return -1;
    }
    nv->ctx      = ctx;
    nv->ipc      = NULL;
    nv->epoll_fd = -1;
    nv->blocking = ctx->config.nvipc.blocking ? 1 : 0;
    atomic_store(&nv->running, 1);
    atomic_store(&nv->rx_count, 0u);
    atomic_store(&nv->tx_count, 0u);

    if (aerial_nvipc_build_config(ctx, nv) != 0) {
        free(nv);
        return -1;
    }

    oc->nvipc = nv;

    int rc = pthread_create(&nv->rx_tid, NULL, aerial_nvipc_rx_thread, nv);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[AERIAL_OCUDU] nvIPC RX pthread_create failed: %d", rc);
        oc->nvipc = NULL;
        free(nv);
        return -1;
    }
    nv->rx_started = 1;

    int core = ctx->config.aerial_forwarder.recv_core_id;
    if (core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
        if (pthread_setaffinity_np(nv->rx_tid, sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[AERIAL_OCUDU] nvIPC RX CPU pin to %d failed", core);
        }
    }
    return 0;
}

void aerial_nvipc_stop(AppContext* ctx)
{
    if (ctx == NULL) return;
    AERIALOCUDUContext* oc = &ctx->aerial_ocudu_ctx;
    aerial_nvipc_t* nv = oc->nvipc;
    if (nv == NULL) return;

    atomic_store(&nv->running, 0);
    if (nv->rx_started) {
        pthread_join(nv->rx_tid, NULL);
        nv->rx_started = 0;
    }
    if (nv->epoll_fd >= 0) {
        close(nv->epoll_fd);
        nv->epoll_fd = -1;
    }
    if (nv->ipc != NULL) {
        nv->ipc->ipc_destroy(nv->ipc);
        nv->ipc = NULL;
    }
    SM_Logs(LOG_INFO, _XFAPI_,
            "[AERIAL_OCUDU] nvIPC secondary stopped (rx=%u tx=%u).",
            atomic_load(&nv->rx_count), atomic_load(&nv->tx_count));
    oc->nvipc = NULL;
    free(nv);
}

int aerial_nvipc_send(AppContext* ctx, int32_t msg_id, int32_t cell_id,
                      const void* msg, uint32_t msg_len,
                      const void* data, uint32_t data_len)
{
    if (ctx == NULL) return -1;
    aerial_nvipc_t* nv = ctx->aerial_ocudu_ctx.nvipc;
    if (nv == NULL || nv->ipc == NULL) {
        return -1;  // not attached yet
    }
    nv_ipc_t* ipc = nv->ipc;

    nv_ipc_msg_t out;
    memset(&out, 0, sizeof(out));
    out.msg_id    = msg_id;
    out.cell_id   = cell_id;
    out.msg_len   = (int32_t)msg_len;
    out.data_len  = (int32_t)data_len;

    /* Aerial's CUDA/GPU data pools are configured with pool_len=0, so the data
     * part must come from a CPU pool. CPU_DATA is the common case; fall back to
     * CPU_LARGE when the payload exceeds the CPU_DATA buffer size. */
    out.data_pool = NV_IPC_MEMPOOL_CPU_DATA;
    if (data_len > 0) {
        int cpu_data_sz = nv_ipc_get_buf_size(&nv->cfg, NV_IPC_MEMPOOL_CPU_DATA);
        if (cpu_data_sz > 0 && (int32_t)data_len > cpu_data_sz) {
            out.data_pool = NV_IPC_MEMPOOL_CPU_LARGE;
        }
    }

    if (ipc->tx_allocate(ipc, &out, 0) != 0) {
        SM_Logs(LOG_ERROR, _P7_,
                "[AERIAL_OCUDU L2->Aerial] tx_allocate failed "
                "(msg_id=0x%02x msg_len=%u data_len=%u)",
                msg_id, msg_len, data_len);
        return -1;
    }
    if (msg != NULL && msg_len > 0 && out.msg_buf != NULL) {
        memcpy(out.msg_buf, msg, msg_len);
    }
    if (data != NULL && data_len > 0 && out.data_buf != NULL) {
        memcpy(out.data_buf, data, data_len);
    }
    if (ipc->tx_send_msg(ipc, &out) < 0) {
        SM_Logs(LOG_ERROR, _P7_,
                "[AERIAL_OCUDU L2->Aerial] tx_send_msg failed (msg_id=0x%02x)",
                msg_id);
        ipc->tx_release(ipc, &out);
        return -1;
    }
    if (nv->blocking) {
        ipc->tx_tti_sem_post(ipc);
    } else {
        ipc->notify(ipc, 1);
    }
    atomic_fetch_add(&nv->tx_count, 1u);
    return 0;
}

int aerial_nvipc_is_attached(aerial_nvipc_t* nv)
{
    return (nv != NULL && nv->ipc != NULL) ? 1 : 0;
}

#endif /* AERIAL_OCUDU */
