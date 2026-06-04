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

// OCUDU-L2 -> OAI forwarder (OAI_OCUDU mode).
//
// Drains the OCUDU-L2 xSM queue (xFAPI is SLAVE on pair 1; OCUDU-L2 is the
// master) and sends the payload toward the OAI L1 (PNF) over the VNF's
// nFAPI sockets:
//   - P5 control messages (msg_id < 0x80) -> SCTP P5 socket.
//   - P7 data messages (msg_id >= 0x80)   -> UDP P7 socket.
//
// v1 is PURE PASSTHROUGH: the bytes coming from OCUDU-L2 are sent verbatim.
// This will NOT interoperate with a real OAI L1 (OCUDU and OAI speak
// different FAPI dialects) — FAPI translation is a later phase. The
// forwarder proves the xSM-drain -> nFAPI-send transport path.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "forwarder_l2_to_oai.h"

#ifdef OAI_OCUDU

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <time.h>

#include "../../main/app_context.h"
#include "oai_vnf.h"
#include "unified_logger.h"

#define FAPI_P7_TYPE_THRESHOLD 0x80

static uint64_t g_l2_to_oai_drained = 0;
static uint64_t g_l2_to_oai_sent    = 0;
static uint64_t g_l2_to_oai_last_log_ns = 0;

static uint64_t l2_to_oai_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void l2_to_oai_log_if_due(void)
{
    uint64_t now = l2_to_oai_now_ns();
    if (now - g_l2_to_oai_last_log_ns < 1000000000ull) return;
    g_l2_to_oai_last_log_ns = now;
    SM_Logs(LOG_INFO, _XFAPI_,
            "[OAI_OCUDU L2->OAI] drained=%lu sent=%lu",
            (unsigned long)g_l2_to_oai_drained,
            (unsigned long)g_l2_to_oai_sent);
}

// Raw-send a verbatim payload toward the PNF, routed by message type.
static int l2_to_oai_raw_send(oai_vnf_t* v, uint16_t type_id,
                              const void* payload, uint32_t len)
{
    if (type_id >= FAPI_P7_TYPE_THRESHOLD) {
        // P7 (UDP). Requires the PNF P7 address (learned in PARAM.response).
        if (!v->p7_addr_known) {
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_OCUDU L2->OAI] P7 dest unknown; dropping type=0x%04x.",
                    type_id);
            return -1;
        }
        int sent = sendto(v->p7_sock, payload, len, 0,
                          (struct sockaddr*)&v->pnf_p7_addr,
                          sizeof(v->pnf_p7_addr));
        if (sent != (int)len) {
            SM_Logs(LOG_ERROR, _P7_,
                    "[OAI_OCUDU L2->OAI] P7 sendto %d/%u (type=0x%04x).",
                    sent, len, type_id);
            return -1;
        }
        SM_Logs(LOG_DEBUG, _P7_,
                "[OAI_OCUDU L2->OAI] P7 sent type=0x%04x (%u bytes).",
                type_id, len);
        return 0;
    }

    // P5 (SCTP).
    if (v->p5_sock < 0) {
        SM_Logs(LOG_WARN, _P5_,
                "[OAI_OCUDU L2->OAI] no PNF P5 connection; dropping type=0x%04x.",
                type_id);
        return -1;
    }
    int sent = sctp_sendmsg(v->p5_sock, payload, len,
                            (struct sockaddr*)&v->pnf_p5_addr,
                            sizeof(v->pnf_p5_addr),
                            /*ppid*/0, /*flags*/0, /*stream*/0,
                            /*timetolive*/0, /*context*/0);
    if (sent != (int)len) {
        SM_Logs(LOG_ERROR, _P5_,
                "[OAI_OCUDU L2->OAI] P5 sctp_sendmsg %d/%u (type=0x%04x).",
                sent, len, type_id);
        return -1;
    }
    SM_Logs(LOG_DEBUG, _P5_,
            "[OAI_OCUDU L2->OAI] P5 sent type=0x%04x (%u bytes).", type_id, len);
    return 0;
}

static void* fwd_l2_to_oai_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OAIOCUDUContext* oc = &ctx->oai_ocudu_ctx;

    SM_Logs(LOG_INFO, _XFAPI_, "[OAI_OCUDU L2->OAI] thread started");

    while (atomic_load(&oc->fwd_l2_to_oai_running)) {
        xsm_status_t st = XSM_Wait(oc->h_l2, 1000);
        if (st == XSM_ERR_PEER_NOT_READY) continue;
        if (st != XSM_OK) {
            // Timeout or transient: re-check run flag and retry.
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_l2, &msg) == XSM_OK) {
            g_l2_to_oai_drained++;
            void* src_va = XSM_PhysToVirt(oc->h_l2, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XSM_,
                        "[OAI_OCUDU L2->OAI] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            /* Exact details of every message OCUDU-L2 sends. type_id is the
             * value L2 put in the xsm descriptor; payload_size is the byte
             * count; the hex dump is the raw FAPI payload. */
            SM_Logs(LOG_INFO, _XSM_,
                    "[OAI_OCUDU L2->OAI] L2 MSG: type_id=0x%04x size=%u flags=0x%04x pa=0x%lx",
                    msg.type_id, msg.payload_size, msg.flags,
                    (unsigned long)msg.payload_pa);
            SM_Logs_Buffer(LOG_INFO, _XSM_, "[OAI_OCUDU L2->OAI] L2 payload: ",
                           (const uint8_t*)src_va,
                           msg.payload_size > 256 ? 256 : msg.payload_size);

            oai_vnf_t* v = oc->vnf;
            if (v != NULL) {
                (void)l2_to_oai_raw_send(v, msg.type_id, src_va, msg.payload_size);
                g_l2_to_oai_sent++;
            } else {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OAI_OCUDU L2->OAI] VNF not ready; dropping L2 msg "
                        "type=0x%04x.", msg.type_id);
            }

            XSM_ReturnBuffer(oc->h_l2, msg.payload_pa);
        }
        l2_to_oai_log_if_due();
    }

    SM_Logs(LOG_INFO, _XFAPI_, "[OAI_OCUDU L2->OAI] thread exiting");
    return NULL;
}

int ocudu_fwd_l2_to_oai_start(AppContext* ctx)
{
    if (ctx == NULL || ctx->oai_ocudu_ctx.h_l2 == NULL) {
        return -1;
    }
    atomic_store(&ctx->oai_ocudu_ctx.fwd_l2_to_oai_running, 1);

    int rc = pthread_create(&ctx->oai_ocudu_ctx.fwd_l2_to_oai_tid, NULL,
                            fwd_l2_to_oai_main, ctx);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OAI_OCUDU L2->OAI] pthread_create failed: %d", rc);
        return -1;
    }

    int core = ctx->config.oai_forwarder.send_core_id;
    if (core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
        if (pthread_setaffinity_np(ctx->oai_ocudu_ctx.fwd_l2_to_oai_tid,
                                   sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OAI_OCUDU L2->OAI] CPU pin to %d failed", core);
        }
    }
    return 0;
}

void ocudu_fwd_l2_to_oai_stop(AppContext* ctx)
{
    if (ctx == NULL) return;
    atomic_store(&ctx->oai_ocudu_ctx.fwd_l2_to_oai_running, 0);
    pthread_join(ctx->oai_ocudu_ctx.fwd_l2_to_oai_tid, NULL);
}

#endif /* OAI_OCUDU */
