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

#include "aerial_oai_l1_interface.h"

#ifdef AERIAL_OAI

#include "../main/app_context.h"
#include "../core/AERIAL_OAI/aerial_nvipc.h"
#include "unified_logger.h"

/* Bring up the nvIPC secondary client toward the Aerial cuphycontroller PRIMARY.
 * The client attaches in the background and runs the Aerial->OAI RX loop; there
 * is no xSM handle in this mode (the L2 side is OAI over sockets). */
static int aerial_oai_l1_init(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    if (aerial_nvipc_start(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[AERIAL_OAI] nvIPC client start failed.");
        return -1;
    }
    SM_Logs(LOG_INFO, _XFAPI_,
            "[AERIAL_OAI] L1 (Aerial/nvIPC secondary) endpoint ready.");
    return 0;
}

static void aerial_oai_l1_destroy(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    aerial_nvipc_stop(ctx);
    SM_Logs(LOG_INFO, _XFAPI_, "[AERIAL_OAI] L1 endpoint closed.");
}

static const AERIAL_OAI_L1_Interface g_aerial_oai_l1_interface = {
    .init     = aerial_oai_l1_init,
    .send_msg = NULL,
    .destroy  = aerial_oai_l1_destroy,
};

const AERIAL_OAI_L1_Interface* get_aerial_oai_l1_interface(void)
{
    return &g_aerial_oai_l1_interface;
}

#endif /* AERIAL_OAI */
