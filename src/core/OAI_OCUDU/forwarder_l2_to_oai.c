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

// OCUDU-L2 -> OAI forwarder (OAI_OCUDU mode): drain the OCUDU-L2 xSM queue and
// route each message to nFAPI P5 (SCTP, type<0x80) or P7 (UDP, type>=0x80).

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
#include "oai_l2_to_l1_p5.h"
#include "oai_l2_to_l1_p7.h"
#include "unified_logger.h"

#define FAPI_P7_TYPE_THRESHOLD 0x80

// Max time the forwarder waits for the PNF handshake to reach RUNNING before
// forwarding a cell-level P5 message (OCUDU may send PARAM.request the instant
// it attaches, possibly racing ahead of the PNF handshake). Bounded so a dead
// PNF can't wedge the drain loop forever.
#define P5_RUNNING_WAIT_MS    5000
#define P5_RUNNING_POLL_MS    10

// Wait (bounded) for the PNF handshake to reach RUNNING. Returns 1 if running,
// 0 if the wait timed out (PNF not up).
static int l2_to_oai_wait_pnf_running(AppContext* ctx, oai_vnf_t* v)
{
    int waited = 0;
    while (oai_vnf_get_pnf_state(v) != OAI_VNF_PNF_RUNNING) {
        if (!atomic_load(&ctx->oai_ocudu_ctx.fwd_l2_to_oai_running)) return 0;
        if (waited >= P5_RUNNING_WAIT_MS) return 0;
        struct timespec ts = { .tv_sec = 0,
                               .tv_nsec = (long)P5_RUNNING_POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
        waited += P5_RUNNING_POLL_MS;
    }
    return 1;
}

// Forward one OCUDU-L2 message toward the PNF, routed by message type:
//   - control (type_id < 0x80): translate OCUDU-FAPI -> nFAPI, send over P5.
//   - data    (type_id >= 0x80): translate OCUDU-FAPI -> nFAPI and send over
//     P7. Types without a translator yet (and OCUDU's end-of-slot sentinel)
//     are dropped inside the P7 dispatcher; raw OCUDU-FAPI bytes are never
//     forwarded (OAI speaks a different P7 dialect).
// payload/len is the full xSM buffer (OCUDU header + body).
static int l2_to_oai_forward(AppContext* ctx, oai_vnf_t* v, uint16_t type_id,
                             const void* payload, uint32_t len)
{
    if (type_id >= FAPI_P7_TYPE_THRESHOLD) {
        int r = ocudu_p7_translate_and_send(ctx, v, type_id, payload, len);
        return (r < 0) ? -1 : 0;
    }

    // P5 (SCTP) control: gate on the PNF handshake, then translate + send.
    if (!l2_to_oai_wait_pnf_running(ctx, v)) {
        SM_Logs(LOG_WARN, _P5_,
                "[OAI_OCUDU L2->OAI] PNF not RUNNING; dropping cell P5 "
                "type=0x%04x.", type_id);
        return -1;
    }
    return ocudu_p5_translate_and_send(v, type_id, payload, len);
}

static void* fwd_l2_to_oai_main(void* arg)
{
    AppContext* ctx = (AppContext*)arg;
    OAIOCUDUContext* oc = &ctx->oai_ocudu_ctx;

    while (atomic_load(&oc->fwd_l2_to_oai_running)) {
        xsm_status_t st = XSM_Wait(oc->h_l2, 1000);
        if (st == XSM_ERR_PEER_NOT_READY) continue;
        if (st != XSM_OK) {
            // Timeout or transient: re-check run flag and retry.
            continue;
        }

        xsm_msg_t msg;
        while (XSM_Get(oc->h_l2, &msg) == XSM_OK) {
            void* src_va = XSM_PhysToVirt(oc->h_l2, msg.payload_pa);
            if (src_va == NULL) {
                SM_Logs(LOG_ERROR, _XSM_,
                        "[OAI_OCUDU L2->OAI] PA->VA failed for pa=0x%lx",
                        (unsigned long)msg.payload_pa);
                continue;
            }

            oai_vnf_t* v = oc->vnf;
            if (v != NULL) {
                (void)l2_to_oai_forward(ctx, v, msg.type_id, src_va,
                                        msg.payload_size);
            } else {
                SM_Logs(LOG_WARN, _XFAPI_,
                        "[OAI_OCUDU L2->OAI] VNF not ready; dropping L2 msg "
                        "type=0x%04x.", msg.type_id);
            }

            XSM_ReturnBuffer(oc->h_l2, msg.payload_pa);
        }
    }

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
