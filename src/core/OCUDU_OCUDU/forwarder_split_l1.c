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
 * Split-mode (role=L1) forwarders. Two threads:
 *
 *   l1_to_eth: drains the local memzone (h_l1, master on pair 0; OCUDU-L1
 *              is the slave). For each message read, allocates a buffer
 *              from the DPDK-Eth handle's local pool (via XSM_AcquireBuffer
 *              on h_eth), copies the payload, releases the source buffer
 *              back to OCUDU-L1, then XSM_Put on h_eth. Drives the
 *              pair0_master_pool slot rotation on SLOT.indications.
 *
 *   eth_to_l1: symmetric in reverse. Drains h_eth, allocates from the
 *              pair0_master_pool (master-side caller pool), copies the
 *              payload, returns the source buffer back to h_eth's local
 *              pool, then XSM_Put on h_l1.
 *
 * Same shape as forwarder_l1_to_l2.c / forwarder_l2_to_l1.c; the only
 * difference is one of the two handles is XSM_TRANSPORT_DPDK_ETH.
 * libxsm's vtable dispatch makes that transparent from this file's
 * perspective.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* CPU_ZERO/CPU_SET, pthread_setaffinity_np */
#endif

#include "forwarder_split_l1.h"

#ifdef OCUDU_OCUDU

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "../../main/app_context.h"
#include <stdio.h>
#include <time.h>

#include "pair0_master_pool.h"
#include "unified_logger.h"

/* Mirror the FAPI msg-type id for SLOT.indication. Same constant as the
 * SPLIT_NONE forwarders (see forwarder_l1_to_l2.c). */
#define FAPI_MSG_TYPE_SLOT_INDICATION  0x82

/* ====== Per-thread message-flow counters ==============================
 * Each counter is incremented by exactly one thread (single producer);
 * x86_64 8-byte aligned loads/stores are atomic enough for diagnostic
 * reads from the periodic logger. */
static uint64_t g_l1_to_eth_drained = 0;  /* XSM_Get'd from h_l1 */
static uint64_t g_l1_to_eth_sent    = 0;  /* XSM_Put'd onto h_eth */
static uint64_t g_eth_to_l1_drained = 0;  /* XSM_Get'd from h_eth */
static uint64_t g_eth_to_l1_sent    = 0;  /* XSM_Put'd onto h_l1 */
static uint64_t g_l1_last_log_ns    = 0;

static uint64_t l1_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void l1_log_counters_if_due(void)
{
    uint64_t now = l1_now_ns();
    if (now - g_l1_last_log_ns < 1000000000ull) return;
    g_l1_last_log_ns = now;
    SM_Logs(LOG_INFO, _XFAPI_,
            "[SPLIT L1] l1->eth drained=%lu sent=%lu  eth->l1 drained=%lu sent=%lu",
            (unsigned long)g_l1_to_eth_drained,
            (unsigned long)g_l1_to_eth_sent,
            (unsigned long)g_eth_to_l1_drained,
            (unsigned long)g_eth_to_l1_sent);
}

/* ---- l1_to_eth: local memzone -> network ----------------------------- */
static void* fwd_split_l1_to_eth_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OCUDUContext* oc = &ctx->ocudu_ctx;

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT L1->ETH] thread started");

    while (atomic_load(&oc->forwarders_running)) {
        xsm_status_t st = XSM_Wait(oc->h_l1, 1000);
        if (st == XSM_ERR_PEER_NOT_READY) continue;
        if (st != XSM_OK) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT L1->ETH] XSM_Wait returned %s",
                    xsm_strerror(st));
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_l1, &msg) == XSM_OK) {
            g_l1_to_eth_drained++;
            void* src_va = XSM_PhysToVirt(oc->h_l1, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT L1->ETH] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            /* Acquire a destination buffer in the DPDK-Eth handle's local
             * pool. XSM_Put on h_eth segments + transmits from this VA. */
            uint64_t dst_pa = 0;
            xsm_status_t a_st = XSM_AcquireBuffer(oc->h_eth, &dst_pa);
            if (a_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_SPLIT L1->ETH] XSM_AcquireBuffer (eth) "
                        "failed: %s. Returning L1 buffer and dropping.",
                        xsm_strerror(a_st));
                XSM_ReturnBuffer(oc->h_l1, msg.payload_pa);
                continue;
            }
            void* dst_va = XSM_PhysToVirt(oc->h_eth, dst_pa);
            if (dst_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT L1->ETH] PA->VA failed for eth pa=0x%lx",
                        (unsigned long)dst_pa);
                XSM_ReturnBuffer(oc->h_eth, dst_pa);
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
            xsm_status_t p_st = XSM_Put(oc->h_eth, &out);
            if (p_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_SPLIT L1->ETH] XSM_Put failed: %s",
                        xsm_strerror(p_st));
                XSM_ReturnBuffer(oc->h_eth, dst_pa);
                continue;
            }
            /* xsm_eth_put has copied the bytes onto the wire (allocated
             * its own mbufs from the TX pool, segmented, tx_burst'd). The
             * staging buffer dst_pa is no longer needed; return it to the
             * local DPDK-Eth pool, otherwise we leak one buffer per
             * forwarded message and the pool exhausts after ~1024 msgs. */
            XSM_ReturnBuffer(oc->h_eth, dst_pa);
            g_l1_to_eth_sent++;

            /* On the DPDK-Eth transport, XSM_Notify is implicit in the
             * wire send (and a counted no-op locally), but we call it for
             * symmetry with the memzone path. */
            XSM_Notify(oc->h_eth);

            /* Slot-rotation driver. SLOT.indications arrive from L1 here
             * (same logical position as forwarder_l1_to_l2.c). */
            if (msg.type_id == FAPI_MSG_TYPE_SLOT_INDICATION) {
                pair0_master_pool_on_slot_indication(oc->pair0_master_pool);
            }
        }
        l1_log_counters_if_due();
    }

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT L1->ETH] thread exiting");
    return NULL;
}

/* ---- eth_to_l1: network -> local memzone ----------------------------- */
static void* fwd_split_eth_to_l1_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OCUDUContext* oc = &ctx->ocudu_ctx;

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT ETH->L1] thread started");

    while (atomic_load(&oc->forwarders_running)) {
        xsm_status_t st = XSM_Wait(oc->h_eth, 1000);
        if (st == XSM_ERR_PEER_NOT_READY) continue;
        if (st != XSM_OK) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT ETH->L1] XSM_Wait returned %s",
                    xsm_strerror(st));
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_eth, &msg) == XSM_OK) {
            g_eth_to_l1_drained++;
            void* src_va = XSM_PhysToVirt(oc->h_eth, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT ETH->L1] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            /* Destination buffer: pair0 master pool (slot-rotation
             * deferred free; mirrors forwarder_l2_to_l1.c). */
            void* dst_va = pair0_master_pool_acquire(oc->pair0_master_pool);
            if (dst_va == NULL) {
                XSM_ReturnBuffer(oc->h_eth, msg.payload_pa);
                continue;
            }
            uint64_t dst_pa = pair0_master_pool_va_to_pa(
                oc->pair0_master_pool, oc->h_l1, dst_va);
            if (dst_pa == 0u) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "[OCUDU_SPLIT ETH->L1] VA->PA failed for master va=%p",
                        dst_va);
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
            xsm_status_t p_st = XSM_Put(oc->h_l1, &out);
            if (p_st != XSM_OK) {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OCUDU_SPLIT ETH->L1] XSM_Put failed: %s",
                        xsm_strerror(p_st));
                continue;
            }
            g_eth_to_l1_sent++;
            XSM_Notify(oc->h_l1);
        }
        l1_log_counters_if_due();
    }

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_SPLIT ETH->L1] thread exiting");
    return NULL;
}

int ocudu_fwd_split_l1_start(AppContext* ctx)
{
    if (ctx == NULL ||
        ctx->ocudu_ctx.h_l1 == NULL ||
        ctx->ocudu_ctx.h_eth == NULL) {
        return -1;
    }
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 1);

    int rc = pthread_create(&ctx->ocudu_ctx.fwd_split_to_net_tid, NULL,
                            fwd_split_l1_to_eth_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT L1->ETH] pthread_create failed: %d", rc);
        return -1;
    }
    rc = pthread_create(&ctx->ocudu_ctx.fwd_split_from_net_tid, NULL,
                        fwd_split_eth_to_l1_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_SPLIT ETH->L1] pthread_create failed: %d", rc);
        return -1;
    }

    /* Optional CPU pinning (reuses the existing ocudu_forwarder fields).
     *   l1_to_l2_core_id pins the local->network thread.
     *   l2_to_l1_core_id pins the network->local thread.
     * Naming is awkward in split mode but the semantic carries over. */
    int core_to_net = ctx->config.ocudu_forwarder.l1_to_l2_core_id;
    int core_from_net = ctx->config.ocudu_forwarder.l2_to_l1_core_id;
    if (core_to_net >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core_to_net, &set);
        if (pthread_setaffinity_np(ctx->ocudu_ctx.fwd_split_to_net_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT L1->ETH] CPU pin to %d failed", core_to_net);
        }
    }
    if (core_from_net >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core_from_net, &set);
        if (pthread_setaffinity_np(ctx->ocudu_ctx.fwd_split_from_net_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_SPLIT ETH->L1] CPU pin to %d failed", core_from_net);
        }
    }
    return 0;
}

void ocudu_fwd_split_l1_stop(AppContext* ctx)
{
    if (ctx == NULL) return;
    atomic_store(&ctx->ocudu_ctx.forwarders_running, 0);
    /* Both threads loop with a 1-second XSM_Wait timeout, so they notice
     * the stop flag within ~1s. */
    pthread_join(ctx->ocudu_ctx.fwd_split_to_net_tid, NULL);
    pthread_join(ctx->ocudu_ctx.fwd_split_from_net_tid, NULL);
}

#endif /* OCUDU_OCUDU */
