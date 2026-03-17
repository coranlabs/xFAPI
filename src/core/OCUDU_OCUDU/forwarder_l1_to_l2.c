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


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "forwarder_l1_to_l2.h"

#ifdef OCUDU_OCUDU

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "../../main/app_context.h"
#include "pair0_master_pool.h"
#include "unified_logger.h"

#define FAPI_MSG_TYPE_SLOT_INDICATION  0x82

static void* fwd_l1_to_l2_thread_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OCUDUContext* oc = &ctx->ocudu_ctx;

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_FWD L1->L2] thread started");

    while (atomic_load(&oc->forwarders_running)) {

        xsm_status_t st = XSM_Wait(oc->h_l1, 1000 );
        if (st == XSM_ERR_PEER_NOT_READY) {

            continue;
        }
        if (st != XSM_OK) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_FWD L1->L2] XSM_Wait returned %s", xsm_strerror(st));
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_l1, &msg) == XSM_OK) {
            void* src_va = XSM_PhysToVirt(oc->h_l1, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_FWD L1->L2] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            uint64_t dst_pa = 0;
            xsm_status_t a_st = XSM_AcquireBuffer(oc->h_l2, &dst_pa);
            if (a_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_FWD L1->L2] XSM_AcquireBuffer (L2 side) failed: %s. Returning L1 buffer and dropping.",
                        xsm_strerror(a_st));
                XSM_ReturnBuffer(oc->h_l1, msg.payload_pa);
                continue;
            }
            void* dst_va = XSM_PhysToVirt(oc->h_l2, dst_pa);
            if (dst_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_FWD L1->L2] PA->VA failed for L2-side pa=0x%lx",
                        (unsigned long)dst_pa);
                XSM_ReturnBuffer(oc->h_l1, msg.payload_pa);
                continue;
            }

            memcpy(dst_va, src_va, msg.payload_size);

            XSM_ReturnBuffer(oc->h_l1, msg.payload_pa);

            xsm_msg_t out = {
                .payload_pa   = dst_pa,
                .payload_size = msg.payload_size,
                .type_id      = msg.type_id,
                .flags        = msg.flags,
            };
            xsm_status_t p_st = XSM_Put(oc->h_l2, &out);
            if (p_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_FWD L1->L2] XSM_Put failed: %s",
                        xsm_strerror(p_st));
                continue;
            }
            XSM_Notify(oc->h_l2);

            if (msg.type_id == FAPI_MSG_TYPE_SLOT_INDICATION) {
                pair0_master_pool_on_slot_indication(oc->pair0_master_pool);
            }
        }
    }

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_FWD L1->L2] thread exiting");
    return NULL;
}

int ocudu_fwd_l1_to_l2_start(AppContext* ctx)
{
    if (ctx == NULL || ctx->ocudu_ctx.h_l1 == NULL || ctx->ocudu_ctx.h_l2 == NULL) {
        return -1;
    }
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 1);

    int rc = pthread_create(&ctx->ocudu_ctx.fwd_l1_to_l2_tid, NULL,
                            fwd_l1_to_l2_thread_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_FWD L1->L2] pthread_create failed: %d", rc);
        return -1;
    }

    int core = ctx->config.ocudu_forwarder.l1_to_l2_core_id;
    if (core >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core, &set);
        if (pthread_setaffinity_np(ctx->ocudu_ctx.fwd_l1_to_l2_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_FWD L1->L2] CPU pin to %d failed", core);
        }
    }
    return 0;
}

void ocudu_fwd_l1_to_l2_stop(AppContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 0);

    pthread_join(ctx->ocudu_ctx.fwd_l1_to_l2_tid, NULL);
}

#endif
