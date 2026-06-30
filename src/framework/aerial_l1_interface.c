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

#include "aerial_l1_interface.h"

#ifdef AERIAL_OCUDU

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "../main/app_context.h"
#include "../core/AERIAL_OCUDU/aerial_nvipc.h"
#include "unified_logger.h"

static void aerial_wait_peer_ready(xsm_handle_t *handle,
                                   const char *label,
                                   uint32_t budget_ms)
{
    uint32_t waited_ms = 0;
    const uint32_t poll_ms = 100;
    const uint32_t log_every_ms = 5000;
    uint32_t since_log_ms = 0;
    int warned = 0;
    while (XSM_IsPeerReady(handle) != XSM_OK) {
        usleep(poll_ms * 1000);
        waited_ms += poll_ms;
        since_log_ms += poll_ms;
        if (since_log_ms >= log_every_ms) {
            SM_Logs(LOG_INFO, _XFAPI_,
                    "[AERIAL_OCUDU] still waiting for %s peer (%us elapsed)…",
                    label, waited_ms / 1000);
            since_log_ms = 0;
        }
        if (!warned && waited_ms >= budget_ms) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[AERIAL_OCUDU] %s peer not ready after %us; continuing to poll.",
                    label, budget_ms / 1000);
            warned = 1;
        }
    }
    SM_Logs(LOG_INFO, _XFAPI_,
            "[AERIAL_OCUDU] %s peer ready after ~%us.", label, waited_ms / 1000);
}

/* Open + allocate the xSM handle toward OCUDU-L2 WITHOUT blocking for the
 * peer. xFAPI owns the memzone (the role OCUDU-L1 plays in OCUDU_OCUDU):
 * SLAVE on pair 1; OCUDU-L2 attaches as MASTER on pair 1. The peer-ready
 * wait happens asynchronously so the nvIPC client can come up and attach to
 * the Aerial PRIMARY before OCUDU-L2 attaches. */
static int aerial_open_l2_xsm(struct AppContext* ctx)
{
    const ocudu_xsm_endpoint_config_t* ep = &ctx->config.ocudu_xsm_l2;
    AERIALOCUDUContext* oc = &ctx->aerial_ocudu_ctx;

    xsm_config_t cfg_l2;
    memset(&cfg_l2, 0, sizeof(cfg_l2));
    strncpy(cfg_l2.device_name, ep->device_name, XSM_DEVICE_NAME_MAX - 1);
    cfg_l2.role                  = XSM_ROLE_SLAVE;
    cfg_l2.memory_size           = ep->memory_size;
    cfg_l2.queue_capacity        = ep->queue_capacity;
    cfg_l2.return_queue_capacity = ep->return_queue_capacity;
    cfg_l2.wakeup_mode           = XSM_WAKEUP_POSIX_SEM;
    cfg_l2.pair_index            = 1;
    cfg_l2.num_pairs             = 2u;
    cfg_l2.transport             = XSM_TRANSPORT_MEMZONE;

    xsm_status_t st = XSM_Open(&cfg_l2, &oc->h_l2);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[AERIAL_OCUDU] XSM_Open (L2, pair 1 slave) failed: %s",
                xsm_strerror(st));
        return -1;
    }

    st = XSM_Alloc(oc->h_l2, ep->memory_size, &oc->region_l2);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[AERIAL_OCUDU] XSM_Alloc on h_l2 (pair 1 slave) failed: %s",
                xsm_strerror(st));
        XSM_Close(oc->h_l2);
        oc->h_l2 = NULL;
        return -1;
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[AERIAL_OCUDU] xSM L2 ready (memzone '%s', pair 1, region=%p). "
            "OCUDU-L2 may attach as master at any time.",
            ep->device_name, oc->region_l2);
    return 0;
}

/* Background thread: wait for OCUDU-L2 to attach on pair 1, then log. The
 * L2->Aerial forwarder already tolerates XSM_ERR_PEER_NOT_READY, so traffic
 * flows automatically once the peer is up — this thread is purely for
 * observability and does not gate anything. */
static void* aerial_l2_peer_wait_thread(void* arg)
{
    struct AppContext* ctx = (struct AppContext*)arg;
    aerial_wait_peer_ready(ctx->aerial_ocudu_ctx.h_l2, "OCUDU-L2", 120u * 1000u);
    return NULL;
}

static int aerial_l1_init(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    AERIALOCUDUContext* oc = &ctx->aerial_ocudu_ctx;

    /* 1. Open the L2 xSM handle (non-blocking). */
    if (aerial_open_l2_xsm(ctx) != 0) {
        return -1;
    }

    /* 2. Bring up the nvIPC secondary client. It attaches to the Aerial
     *    PRIMARY in the background and runs the Aerial->L2 RX loop. */
    if (aerial_nvipc_start(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[AERIAL_OCUDU] nvIPC client start failed.");
        if (oc->h_l2 != NULL) {
            if (oc->region_l2 != NULL) { XSM_Free(oc->h_l2, oc->region_l2); oc->region_l2 = NULL; }
            XSM_Close(oc->h_l2);
            oc->h_l2 = NULL;
        }
        return -1;
    }

    /* 3. Watch for OCUDU-L2 attach in the background (non-gating). */
    if (pthread_create(&oc->l2_peer_wait_tid, NULL,
                       aerial_l2_peer_wait_thread, ctx) == 0) {
        oc->l2_peer_wait_started = 1;
        pthread_detach(oc->l2_peer_wait_tid);
    } else {
        SM_Logs(LOG_WARN, _XFAPI_,
                "[AERIAL_OCUDU] could not spawn L2 peer-wait thread (non-fatal).");
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[AERIAL_OCUDU] L1 (Aerial/nvIPC secondary) endpoint ready.");
    return 0;
}

static void aerial_l1_destroy(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    AERIALOCUDUContext* oc = &ctx->aerial_ocudu_ctx;

    /* Stop the nvIPC client (joins RX thread, destroys instance, frees state). */
    aerial_nvipc_stop(ctx);

    if (oc->h_l2 != NULL) {
        if (oc->region_l2 != NULL) {
            XSM_Free(oc->h_l2, oc->region_l2);
            oc->region_l2 = NULL;
        }
        XSM_Close(oc->h_l2);
        oc->h_l2 = NULL;
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[AERIAL_OCUDU] L1 endpoint closed.");
}

static const AERIAL_L1_Interface g_aerial_l1_interface = {
    .init     = aerial_l1_init,
    .send_msg = NULL,
    .destroy  = aerial_l1_destroy,
};

const AERIAL_L1_Interface* get_aerial_l1_interface(void)
{
    return &g_aerial_l1_interface;
}

#endif /* AERIAL_OCUDU */
