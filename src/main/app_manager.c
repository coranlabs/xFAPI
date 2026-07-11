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

#include "app_context.h"
#include "../sim/common/sim_common.h"
#ifdef OCUDU_OCUDU
#include "../core/OCUDU_OCUDU/forwarder_l1_to_l2.h"
#include "../core/OCUDU_OCUDU/forwarder_l2_to_l1.h"
#include "../framework/dpdk/dpdk_init.h"
#endif
#ifdef OAI_OCUDU
#include "../core/OAI_OCUDU/forwarder_l2_to_oai.h"
#include "../framework/dpdk/dpdk_init.h"
#endif
#ifdef AERIAL_OCUDU
#include "../core/AERIAL_OCUDU/forwarder_l2_to_aerial.h"
#include "../framework/dpdk/dpdk_init.h"
#endif
#ifdef AERIAL_OAI
#include "../framework/aerial_oai_l1_interface.h"
#include "../framework/oai_l2_interface.h"
#endif

static void app_select_interfaces(AppContext* ctx) {
#ifdef OCUDU_OCUDU
    SM_Logs(LOG_INFO, _XFAPI_, "Selecting xSM bridge interfaces.");
    ctx->ocudu_l1_ctx = get_ocudu_l1_interface();
    ctx->ocudu_l2_ctx = get_ocudu_l2_interface();
    SM_Logs(LOG_INFO, _XFAPI_, "L1 and L2 interface selection successful.");
#elif defined(OAI_OCUDU)
    SM_Logs(LOG_INFO, _XFAPI_, "Selecting OAI L1 (nFAPI) + OCUDU L2 (xSM) interfaces.");
    ctx->oai_l1_ctx   = get_oai_l1_interface();
    ctx->ocudu_l2_ctx = get_ocudu_l2_interface();
    SM_Logs(LOG_INFO, _XFAPI_, "L1 and L2 interface selection successful.");
#elif defined(AERIAL_OCUDU)
    SM_Logs(LOG_INFO, _XFAPI_, "Selecting Aerial L1 (nvIPC) + OCUDU L2 (xSM) interfaces.");
    ctx->aerial_l1_ctx = get_aerial_l1_interface();
    ctx->ocudu_l2_ctx  = get_ocudu_l2_interface();
    SM_Logs(LOG_INFO, _XFAPI_, "L1 and L2 interface selection successful.");
#elif defined(AERIAL_OAI)
    SM_Logs(LOG_INFO, _XFAPI_, "Selecting Aerial L1 (nvIPC) + OAI L2 (nFAPI) interfaces.");
    ctx->aerial_l1_ctx = get_aerial_oai_l1_interface();
    ctx->oai_l2_ctx    = get_oai_l2_interface();
    SM_Logs(LOG_INFO, _XFAPI_, "L1 and L2 interface selection successful.");
#else
    SM_Logs(LOG_CRTERR, _XFAPI_, "No valid mode defined!");
    (void)ctx;
#endif
}

int app_init(AppContext* ctx) {
    app_select_interfaces(ctx);

#ifdef OCUDU_OCUDU

    if (dpdk_init_ocudu_bridge(&ctx->config) != SUCCESS) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "DPDK init failed for OCUDU_OCUDU mode.");
        return -1;
    }
    if (!ctx->ocudu_l1_ctx || !ctx->ocudu_l2_ctx) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OCUDU L1/L2 interfaces not selected.");
        return -1;
    }
    if (ctx->ocudu_l1_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OCUDU L1 endpoint init failed.");
        return -1;
    }
    if (ctx->ocudu_l2_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OCUDU L2 endpoint init failed.");
        ctx->ocudu_l1_ctx->destroy(ctx);
        return -1;
    }
    if (ocudu_fwd_l1_to_l2_start(ctx) != 0 ||
        ocudu_fwd_l2_to_l1_start(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OCUDU forwarder thread start failed.");
        return -1;
    }
    return 0;
#elif defined(OAI_OCUDU)

    if (dpdk_init_ocudu_bridge(&ctx->config) != SUCCESS) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "DPDK init failed for OAI_OCUDU mode.");
        return -1;
    }
    if (!ctx->oai_l1_ctx || !ctx->ocudu_l2_ctx) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OAI L1 / OCUDU L2 interfaces not selected.");
        return -1;
    }
    /* L1 init opens the UDP sockets AND the xSM handle toward OCUDU-L2. */
    if (ctx->oai_l1_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OAI L1 endpoint init failed.");
        return -1;
    }
    /* L2 init is a no-op (xSM handle is opened in L1 init); keep flow symmetric. */
    if (ctx->ocudu_l2_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OCUDU L2 endpoint init failed.");
        ctx->oai_l1_ctx->destroy(ctx);
        return -1;
    }
    /* L2->OAI forwarder: drains the OCUDU-L2 xSM queue and sends toward OAI
     * over the VNF's nFAPI sockets. The OAI->L2 (uplink) direction is handled
     * inside the VNF's P7 rx_task (started on PNF_START.response). */
    if (ocudu_fwd_l2_to_oai_start(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OAI_OCUDU L2->OAI forwarder start failed.");
        return -1;
    }
    return 0;
#elif defined(AERIAL_OCUDU)

    if (dpdk_init_ocudu_bridge(&ctx->config) != SUCCESS) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "DPDK init failed for AERIAL_OCUDU mode.");
        return -1;
    }
    if (!ctx->aerial_l1_ctx || !ctx->ocudu_l2_ctx) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "Aerial L1 / OCUDU L2 interfaces not selected.");
        return -1;
    }
    /* L1 init opens the L2 xSM handle AND starts the nvIPC secondary client
     * (which attaches to the Aerial PRIMARY in the background). */
    if (ctx->aerial_l1_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "Aerial L1 endpoint init failed.");
        return -1;
    }
    /* L2 init is a no-op (xSM handle is opened in L1 init); keep flow symmetric. */
    if (ctx->ocudu_l2_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OCUDU L2 endpoint init failed.");
        ctx->aerial_l1_ctx->destroy(ctx);
        return -1;
    }
    /* L2->Aerial forwarder: drains the OCUDU-L2 xSM queue and (later) sends the
     * translated SCF FAPI message over nvIPC. The Aerial->L2 (uplink)
     * direction is handled inside the nvIPC client's RX thread. */
    if (ocudu_fwd_l2_to_aerial_start(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "AERIAL_OCUDU L2->Aerial forwarder start failed.");
        return -1;
    }
    return 0;
#elif defined(AERIAL_OAI)

    if (!ctx->aerial_l1_ctx || !ctx->oai_l2_ctx) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "Aerial L1 / OAI L2 interfaces not selected.");
        return -1;
    }
    /* L1 init starts the nvIPC secondary (attaches to Aerial in the background
     * and runs the Aerial->OAI RX loop). No DPDK/xSM in this mode. */
    if (ctx->aerial_l1_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "Aerial L1 endpoint init failed.");
        return -1;
    }
    /* L2 init opens the nFAPI PNF (SCTP P5 + UDP P7), the handshake responder,
     * and the OAI->Aerial forwarder. */
    if (ctx->oai_l2_ctx->init(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "OAI L2 endpoint init failed.");
        ctx->aerial_l1_ctx->destroy(ctx);
        return -1;
    }
    return 0;
#else
    SM_Logs(LOG_CRTERR, _XFAPI_, "No build mode defined.");
    return -1;
#endif
}

void app_run(AppContext* ctx) {
    ctx->is_running = 1;
    SM_Logs(LOG_INFO, _XFAPI_, GREEN "🚀 Application running" RESET_COLOR);
    while(ctx->is_running) {

        sleep(1);
    }
}

void app_destroy(AppContext* ctx) {
#ifdef OCUDU_OCUDU
    ocudu_fwd_l1_to_l2_stop(ctx);
    ocudu_fwd_l2_to_l1_stop(ctx);
    if (ctx->ocudu_l2_ctx && ctx->ocudu_l2_ctx->destroy) {
        ctx->ocudu_l2_ctx->destroy(ctx);
    }
    if (ctx->ocudu_l1_ctx && ctx->ocudu_l1_ctx->destroy) {
        ctx->ocudu_l1_ctx->destroy(ctx);
    }
#elif defined(OAI_OCUDU)
    ocudu_fwd_l2_to_oai_stop(ctx);
    if (ctx->ocudu_l2_ctx && ctx->ocudu_l2_ctx->destroy) {
        ctx->ocudu_l2_ctx->destroy(ctx);
    }
    /* oai_l1_ctx->destroy stops the VNF (P5/P7 threads) and closes L2 xSM. */
    if (ctx->oai_l1_ctx && ctx->oai_l1_ctx->destroy) {
        ctx->oai_l1_ctx->destroy(ctx);
    }
#elif defined(AERIAL_OCUDU)
    ocudu_fwd_l2_to_aerial_stop(ctx);
    if (ctx->ocudu_l2_ctx && ctx->ocudu_l2_ctx->destroy) {
        ctx->ocudu_l2_ctx->destroy(ctx);
    }
    /* aerial_l1_ctx->destroy stops the nvIPC client and closes L2 xSM. */
    if (ctx->aerial_l1_ctx && ctx->aerial_l1_ctx->destroy) {
        ctx->aerial_l1_ctx->destroy(ctx);
    }
#elif defined(AERIAL_OAI)
    /* Stop L2 (OAI PNF: P5/P7 threads + sockets) then L1 (nvIPC client). */
    if (ctx->oai_l2_ctx && ctx->oai_l2_ctx->destroy) {
        ctx->oai_l2_ctx->destroy(ctx);
    }
    if (ctx->aerial_l1_ctx && ctx->aerial_l1_ctx->destroy) {
        ctx->aerial_l1_ctx->destroy(ctx);
    }
#else

#endif
    SM_Logs(LOG_INFO, _XFAPI_, "Application cleanup complete.");
}
