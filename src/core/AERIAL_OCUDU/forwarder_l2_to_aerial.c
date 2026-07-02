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

// OCUDU-L2 -> Aerial forwarder (AERIAL_OCUDU mode): drain the OCUDU-L2 xSM
// queue. Scaffold milestone: each OCUDU-FAPI message is logged and dropped.
// Later milestones translate OCUDU-FAPI -> SCF FAPI and forward over nvIPC via
// aerial_nvipc_send().

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "forwarder_l2_to_aerial.h"

#ifdef AERIAL_OCUDU

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include "../../main/app_context.h"
#include "aerial_nvipc.h"
#include "aerial_l2_to_l1_p5.h"
#include "unified_logger.h"

#define FAPI_P7_TYPE_THRESHOLD 0x80

static void l2_to_aerial_forward(AppContext* ctx, uint16_t type_id,
                                 const void* payload, uint32_t len)
{
    if (type_id >= FAPI_P7_TYPE_THRESHOLD) {
        SM_Logs(LOG_DEBUG, _P7_,
                "[AERIAL_OCUDU L2->Aerial] P7 msg type=0x%04x len=%u "
                "(no translator yet); dropping.", type_id, len);
        return;
    }
    (void)aerial_p5_translate_and_send(ctx, type_id, payload, len);
}

static void* fwd_l2_to_aerial_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    AERIALOCUDUContext* oc = &ctx->aerial_ocudu_ctx;

    while (atomic_load(&oc->fwd_l2_to_aerial_running)) {
        xsm_status_t st = XSM_Wait(oc->h_l2, 1000);
        if (st == XSM_ERR_PEER_NOT_READY) continue;
        if (st != XSM_OK) {
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_l2, &msg) == XSM_OK) {
            void* src_va = XSM_PhysToVirt(oc->h_l2, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XSM_,
                        "[AERIAL_OCUDU L2->Aerial] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }
            l2_to_aerial_forward(ctx, msg.type_id, src_va, msg.payload_size);
            XSM_ReturnBuffer(oc->h_l2, msg.payload_pa);
        }
    }
    return NULL;
}

int ocudu_fwd_l2_to_aerial_start(AppContext* ctx)
{
    if (ctx == NULL || ctx->aerial_ocudu_ctx.h_l2 == NULL) {
        return -1;
    }
    atomic_store(&ctx->aerial_ocudu_ctx.fwd_l2_to_aerial_running, 1);

    int rc = pthread_create(&ctx->aerial_ocudu_ctx.fwd_l2_to_aerial_tid, NULL,
                            fwd_l2_to_aerial_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[AERIAL_OCUDU L2->Aerial] pthread_create failed: %d", rc);
        return -1;
    }

    int core = ctx->config.aerial_forwarder.send_core_id;
    if (core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
        if (pthread_setaffinity_np(ctx->aerial_ocudu_ctx.fwd_l2_to_aerial_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[AERIAL_OCUDU L2->Aerial] CPU pin to %d failed", core);
        }
    }
    return 0;
}

void ocudu_fwd_l2_to_aerial_stop(AppContext* ctx)
{
    if (ctx == NULL) return;
    atomic_store(&ctx->aerial_ocudu_ctx.fwd_l2_to_aerial_running, 0);
    pthread_join(ctx->aerial_ocudu_ctx.fwd_l2_to_aerial_tid, NULL);
}

#endif /* AERIAL_OCUDU */
