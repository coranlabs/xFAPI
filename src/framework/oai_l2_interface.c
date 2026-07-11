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

#include "oai_l2_interface.h"

#ifdef AERIAL_OAI

#include "../main/app_context.h"
#include "../core/AERIAL_OAI/oai_pnf.h"
#include "unified_logger.h"

/* Bring up the nFAPI PNF server toward OAI-L2: SCTP P5 listener + UDP P7 socket,
 * the PNF handshake responder, and the OAI->Aerial forwarder. */
static int oai_l2_init(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    if (oai_pnf_start(ctx) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[AERIAL_OAI] OAI PNF start failed.");
        return -1;
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[AERIAL_OAI] L2 (OAI/nFAPI PNF) endpoint ready.");
    return 0;
}

static void oai_l2_destroy(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    oai_pnf_stop(ctx);
    SM_Logs(LOG_INFO, _XFAPI_, "[AERIAL_OAI] L2 endpoint closed.");
}

static const OAI_L2_Interface g_oai_l2_interface = {
    .init     = oai_l2_init,
    .send_msg = NULL,
    .destroy  = oai_l2_destroy,
};

const OAI_L2_Interface* get_oai_l2_interface(void)
{
    return &g_oai_l2_interface;
}

#endif /* AERIAL_OAI */
