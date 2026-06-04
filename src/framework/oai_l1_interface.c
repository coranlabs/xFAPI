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

#include "oai_l1_interface.h"

#ifdef OAI_OCUDU

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "../main/app_context.h"
#include "../core/OAI_OCUDU/oai_vnf.h"
#include "unified_logger.h"

static void oai_wait_peer_ready(xsm_handle_t *handle,
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
                    "[OAI_OCUDU] still waiting for %s peer (%us elapsed)…",
                    label, waited_ms / 1000);
            since_log_ms = 0;
        }
        if (!warned && waited_ms >= budget_ms) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OAI_OCUDU] %s peer not ready after %us; continuing to poll.",
                    label, budget_ms / 1000);
            warned = 1;
        }
    }
    SM_Logs(LOG_INFO, _XFAPI_,
            "[OAI_OCUDU] %s peer ready after ~%us.", label, waited_ms / 1000);
}

/* Open + allocate the xSM handle toward OCUDU-L2 WITHOUT blocking for the
 * peer. xFAPI owns the memzone (the role OCUDU-L1 plays in OCUDU_OCUDU):
 * SLAVE on pair 1; OCUDU-L2 attaches as MASTER on pair 1. The peer-ready
 * wait happens asynchronously (see oai_l2_peer_wait_thread) so the nFAPI
 * VNF can come up and accept the OAI L1 (PNF) before OCUDU-L2 attaches. */
static int oai_open_l2_xsm(struct AppContext* ctx)
{
    const ocudu_xsm_endpoint_config_t* ep = &ctx->config.ocudu_xsm_l2;
    OAIOCUDUContext* oc = &ctx->oai_ocudu_ctx;

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
                "[OAI_OCUDU] XSM_Open (L2, pair 1 slave) failed: %s",
                xsm_strerror(st));
        return -1;
    }

    st = XSM_Alloc(oc->h_l2, ep->memory_size, &oc->region_l2);
    if (st != XSM_OK) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OAI_OCUDU] XSM_Alloc on h_l2 (pair 1 slave) failed: %s",
                xsm_strerror(st));
        XSM_Close(oc->h_l2);
        oc->h_l2 = NULL;
        return -1;
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[OAI_OCUDU] xSM L2 ready (memzone '%s', pair 1, region=%p). "
            "OCUDU-L2 may attach as master at any time.",
            ep->device_name, oc->region_l2);
    return 0;
}

/* Background thread: wait for OCUDU-L2 to attach on pair 1, then log. The
 * L2->OAI forwarder already tolerates XSM_ERR_PEER_NOT_READY, so traffic
 * flows automatically once the peer is up — this thread is purely for
 * observability and does not gate anything. */
static void* oai_l2_peer_wait_thread(void* arg)
{
    struct AppContext* ctx = (struct AppContext*)arg;
    oai_wait_peer_ready(ctx->oai_ocudu_ctx.h_l2, "OCUDU-L2", 120u * 1000u);
    return NULL;
}

static int oai_l1_init(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    OAIOCUDUContext* oc = &ctx->oai_ocudu_ctx;

    /* 1. Open the L2 xSM handle (non-blocking). */
    if (oai_open_l2_xsm(ctx) != 0) {
        return -1;
    }

    /* 2. Bring up the nFAPI VNF FIRST so the SCTP P5 listener + P7 UDP socket
     *    are live immediately — the OAI L1 (PNF) can connect before OCUDU-L2
     *    has attached. On PNF_START.response the VNF starts the P7 data plane. */
    if (oai_vnf_start(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[OAI_OCUDU] VNF start failed.");
        if (oc->h_l2 != NULL) {
            if (oc->region_l2 != NULL) { XSM_Free(oc->h_l2, oc->region_l2); oc->region_l2 = NULL; }
            XSM_Close(oc->h_l2);
            oc->h_l2 = NULL;
        }
        return -1;
    }

    /* 3. Watch for OCUDU-L2 attach in the background (non-gating). Detached:
     *    it returns on its own once the peer is up; if L2 never attaches it
     *    polls harmlessly and is reaped at process exit (does not block
     *    shutdown). */
    if (pthread_create(&oc->l2_peer_wait_tid, NULL,
                       oai_l2_peer_wait_thread, ctx) == 0) {
        oc->l2_peer_wait_started = 1;
        pthread_detach(oc->l2_peer_wait_tid);
    } else {
        SM_Logs(LOG_WARN, _XFAPI_,
                "[OAI_OCUDU] could not spawn L2 peer-wait thread (non-fatal).");
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[OAI_OCUDU] L1 (OAI/nFAPI VNF) endpoint ready; listener accepting "
            "PNF connections.");
    return 0;
}

static void oai_l1_destroy(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    OAIOCUDUContext* oc = &ctx->oai_ocudu_ctx;

    /* Stop the VNF (joins P5/P7 threads, closes nFAPI sockets, frees state). */
    oai_vnf_stop(ctx);

    if (oc->h_l2 != NULL) {
        if (oc->region_l2 != NULL) {
            XSM_Free(oc->h_l2, oc->region_l2);
            oc->region_l2 = NULL;
        }
        XSM_Close(oc->h_l2);
        oc->h_l2 = NULL;
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[OAI_OCUDU] L1 endpoint closed.");
}

static const OAI_L1_Interface g_oai_l1_interface = {
    .init     = oai_l1_init,
    .send_msg = NULL,
    .destroy  = oai_l1_destroy,
};

const OAI_L1_Interface* get_oai_l1_interface(void)
{
    return &g_oai_l1_interface;
}

#endif /* OAI_OCUDU */
