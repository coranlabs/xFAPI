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

/*
 * Split-mode (role=L2) forwarders. Two threads:
 *
 *   l2_to_eth: drain h_l2 (slave on pair 1; OCUDU-L2 is the master) and
 *              relay onto h_eth.
 *   eth_to_l2: drain h_eth and relay onto h_l2.
 *
 * Buffer accounting on the L2 side is purely slave-style: both h_l2 and
 * h_eth are SLAVE roles on their respective backends, so outbound
 * buffers come from XSM_AcquireBuffer on each side, and inbound buffers
 * are returned via XSM_ReturnBuffer.
 *
 * There is NO pair0_master_pool here (that's L1-only — slot-rotation
 * deferred free of master-side buffers).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "forwarder_split_l2.h"

#ifdef OCUDU_OCUDU

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>
#include <time.h>

#include "../../main/app_context.h"
#include "unified_logger.h"

/* ====== Per-thread message-flow counters ============================== */
static uint64_t g_l2_to_eth_drained = 0;
static uint64_t g_l2_to_eth_sent    = 0;
static uint64_t g_eth_to_l2_drained = 0;
static uint64_t g_eth_to_l2_sent    = 0;
static uint64_t g_l2_last_log_ns    = 0;

static uint64_t l2_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void l2_log_counters_if_due(void)
{
    uint64_t now = l2_now_ns();
    if (now - g_l2_last_log_ns < 1000000000ull) return;
    g_l2_last_log_ns = now;
    SM_Logs(LOG_INFO, _XFAPI_,
            "[SPLIT L2] l2->eth drained=%lu sent=%lu  eth->l2 drained=%lu sent=%lu",
            (unsigned long)g_l2_to_eth_drained,
            (unsigned long)g_l2_to_eth_sent,
            (unsigned long)g_eth_to_l2_drained,
            (unsigned long)g_eth_to_l2_sent);
}

/* ---- l2_to_eth: local memzone -> network ----------------------------- */
static void* fwd_split_l2_to_eth_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OCUDUContext* oc = &ctx->ocudu_ctx;

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT L2->ETH] thread started");

    while (atomic_load(&oc->forwarders_running)) {
        xsm_status_t st = XSM_Wait(oc->h_l2, 1000);
        if (st == XSM_ERR_PEER_NOT_READY) continue;
        if (st != XSM_OK) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT L2->ETH] XSM_Wait returned %s",
                    xsm_strerror(st));
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_l2, &msg) == XSM_OK) {
            g_l2_to_eth_drained++;
            void* src_va = XSM_PhysToVirt(oc->h_l2, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT L2->ETH] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            /* Destination buffer from the DPDK-Eth handle's local pool. */
            uint64_t dst_pa = 0;
            xsm_status_t a_st = XSM_AcquireBuffer(oc->h_eth, &dst_pa);
            if (a_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_SPLIT L2->ETH] XSM_AcquireBuffer (eth) "
                        "failed: %s. Returning L2 buffer and dropping.",
                        xsm_strerror(a_st));
                XSM_ReturnBuffer(oc->h_l2, msg.payload_pa);
                continue;
            }
            void* dst_va = XSM_PhysToVirt(oc->h_eth, dst_pa);
            if (dst_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT L2->ETH] PA->VA failed for eth pa=0x%lx",
                        (unsigned long)dst_pa);
                XSM_ReturnBuffer(oc->h_eth, dst_pa);
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
            xsm_status_t p_st = XSM_Put(oc->h_eth, &out);
            if (p_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_SPLIT L2->ETH] XSM_Put failed: %s",
                        xsm_strerror(p_st));
                XSM_ReturnBuffer(oc->h_eth, dst_pa);
                continue;
            }
            /* Bytes are on the wire now (xsm_eth_put owned its own TX
             * mbufs). Return the local staging buffer to the eth pool or
             * we leak one buffer per forwarded message. */
            XSM_ReturnBuffer(oc->h_eth, dst_pa);
            g_l2_to_eth_sent++;
            XSM_Notify(oc->h_eth);
        }
        l2_log_counters_if_due();
    }

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT L2->ETH] thread exiting");
    return NULL;
}

/* ---- eth_to_l2: network -> local memzone ----------------------------- */
static void* fwd_split_eth_to_l2_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OCUDUContext* oc = &ctx->ocudu_ctx;

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT ETH->L2] thread started");

    while (atomic_load(&oc->forwarders_running)) {
        xsm_status_t st = XSM_Wait(oc->h_eth, 1000);
        if (st == XSM_ERR_PEER_NOT_READY) continue;
        if (st != XSM_OK) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT ETH->L2] XSM_Wait returned %s",
                    xsm_strerror(st));
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_eth, &msg) == XSM_OK) {
            g_eth_to_l2_drained++;
            void* src_va = XSM_PhysToVirt(oc->h_eth, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT ETH->L2] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            /* Destination buffer on the L2-side slave bump pool. */
            uint64_t dst_pa = 0;
            xsm_status_t a_st = XSM_AcquireBuffer(oc->h_l2, &dst_pa);
            if (a_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_SPLIT ETH->L2] XSM_AcquireBuffer (l2) "
                        "failed: %s. Returning eth buffer and dropping.",
                        xsm_strerror(a_st));
                XSM_ReturnBuffer(oc->h_eth, msg.payload_pa);
                continue;
            }
            void* dst_va = XSM_PhysToVirt(oc->h_l2, dst_pa);
            if (dst_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT ETH->L2] PA->VA failed for L2 pa=0x%lx",
                        (unsigned long)dst_pa);
                XSM_ReturnBuffer(oc->h_l2, dst_pa);
                XSM_ReturnBuffer(oc->h_eth, msg.payload_pa);
                continue;
            }

            memcpy(dst_va, src_va, msg.payload_size);
            XSM_ReturnBuffer(oc->h_eth, msg.payload_pa);

            xsm_msg_t out = {
                .payload_pa   = dst_pa,
                .payload_size = msg.payload_size,
                .type_id      = msg.type_id,
                .flags        = msg.flags,
            };
            xsm_status_t p_st = XSM_Put(oc->h_l2, &out);
            if (p_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_SPLIT ETH->L2] XSM_Put failed: %s",
                        xsm_strerror(p_st));
                continue;
            }
            g_eth_to_l2_sent++;
            XSM_Notify(oc->h_l2);
        }
        l2_log_counters_if_due();
    }

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT ETH->L2] thread exiting");
    return NULL;
}

int ocudu_fwd_split_l2_start(AppContext* ctx)
{
    if (ctx == NULL ||
        ctx->ocudu_ctx.h_l2 == NULL ||
        ctx->ocudu_ctx.h_eth == NULL) {
        return -1;
    }
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 1);

    int rc = pthread_create(&ctx->ocudu_ctx.fwd_split_to_net_tid, NULL,
                            fwd_split_l2_to_eth_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT L2->ETH] pthread_create failed: %d", rc);
        return -1;
    }
    rc = pthread_create(&ctx->ocudu_ctx.fwd_split_from_net_tid, NULL,
                        fwd_split_eth_to_l2_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT ETH->L2] pthread_create failed: %d", rc);
        return -1;
    }

    int core_to_net   = ctx->config.ocudu_forwarder.l1_to_l2_core_id;
    int core_from_net = ctx->config.ocudu_forwarder.l2_to_l1_core_id;
    if (core_to_net >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core_to_net, &set);
        if (pthread_setaffinity_np(ctx->ocudu_ctx.fwd_split_to_net_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT L2->ETH] CPU pin to %d failed", core_to_net);
        }
    }
    if (core_from_net >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core_from_net, &set);
        if (pthread_setaffinity_np(ctx->ocudu_ctx.fwd_split_from_net_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT ETH->L2] CPU pin to %d failed", core_from_net);
        }
    }
    return 0;
}

void ocudu_fwd_split_l2_stop(AppContext* ctx)
{
    if (ctx == NULL) return;
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 0);
    pthread_join(ctx->ocudu_ctx.fwd_split_to_net_tid, NULL);
    pthread_join(ctx->ocudu_ctx.fwd_split_from_net_tid, NULL);
}

#endif /* OCUDU_OCUDU */
