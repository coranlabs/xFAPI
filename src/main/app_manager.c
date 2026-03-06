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

static void app_select_interfaces(AppContext* ctx) {
#ifdef OCUDU_OCUDU
    SM_Logs(LOG_INFO, _XFAPI_, "Selecting xSM bridge interfaces.");
    ctx->ocudu_l1_ctx = get_ocudu_l1_interface();
    ctx->ocudu_l2_ctx = get_ocudu_l2_interface();
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
#else

#endif
    SM_Logs(LOG_INFO, _XFAPI_, "Application cleanup complete.");
}
