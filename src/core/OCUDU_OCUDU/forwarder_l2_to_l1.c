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

#include "forwarder_l2_to_l1.h"

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

static void* fwd_l2_to_l1_thread_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OCUDUContext* oc = &ctx->ocudu_ctx;

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_FWD L2->L1] thread started");

    while (atomic_load(&oc->forwarders_running)) {
        xsm_status_t st = XSM_Wait(oc->h_l2, 1000 );
        if (st == XSM_ERR_PEER_NOT_READY) {
            continue;
        }
        if (st != XSM_OK) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_FWD L2->L1] XSM_Wait returned %s", xsm_strerror(st));
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_l2, &msg) == XSM_OK) {
            void* src_va = XSM_PhysToVirt(oc->h_l2, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_FWD L2->L1] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            void* dst_va = pair0_master_pool_acquire(oc->pair0_master_pool);
            if (dst_va == NULL) {
                XSM_ReturnBuffer(oc->h_l2, msg.payload_pa);
                continue;
            }
            uint64_t dst_pa = pair0_master_pool_va_to_pa(
                oc->pair0_master_pool, oc->h_l1, dst_va);
            if (dst_pa == 0u) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_FWD L2->L1] VA->PA failed for master-side va=%p",
                        dst_va);
                XSM_ReturnBuffer(oc->h_l2, msg.payload_pa);
                continue;
            }

            memcpy(dst_va, src_va, msg.payload_size);
            XSM_ReturnBuffer(oc->h_l2, msg.payload_pa);

            xsm_msg_t out = {
                .payload_pa   = dst_pa,
                .payload_size = msg.payload_size,
                .type_id      = msg.type_id,
                .flags        = msg.flags,
            };
            xsm_status_t p_st = XSM_Put(oc->h_l1, &out);
            if (p_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_FWD L2->L1] XSM_Put failed: %s",
                        xsm_strerror(p_st));
                continue;
            }
            XSM_Notify(oc->h_l1);
        }
    }

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_FWD L2->L1] thread exiting");
    return NULL;
}

int ocudu_fwd_l2_to_l1_start(AppContext* ctx)
{
    if (ctx == NULL || ctx->ocudu_ctx.h_l1 == NULL || ctx->ocudu_ctx.h_l2 == NULL) {
        return -1;
    }
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 1);

    int rc = pthread_create(&ctx->ocudu_ctx.fwd_l2_to_l1_tid, NULL,
                            fwd_l2_to_l1_thread_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_FWD L2->L1] pthread_create failed: %d", rc);
        return -1;
    }

    int core = ctx->config.ocudu_forwarder.l2_to_l1_core_id;
    if (core >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core, &set);
        if (pthread_setaffinity_np(ctx->ocudu_ctx.fwd_l2_to_l1_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_FWD L2->L1] CPU pin to %d failed", core);
        }
    }
    return 0;
}

void ocudu_fwd_l2_to_l1_stop(AppContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 0);
    pthread_join(ctx->ocudu_ctx.fwd_l2_to_l1_tid, NULL);
}

#endif
