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
 * OCUDU bridge xSM endpoint orchestration (L2 side facade).
 *
 * In the canonical translator-bridge topology, BOTH endpoints are opened
 * by a single XSM_OpenDual call inside ocudu_l1_interface.c::ocudu_l1_init.
 * L2's init becomes a no-op so that the existing app_manager flow (which
 * calls l1->init then l2->init) keeps working without restructuring.
 *
 * destroy() is also a no-op for the same reason — L1's destroy tears down
 * both handles in one shot.
 */

#include "ocudu_l2_interface.h"

#if defined(OCUDU_OCUDU) || defined(OAI_OCUDU) || defined(AERIAL_OCUDU)

#include "../main/app_context.h"
#include "unified_logger.h"

static int ocudu_l2_init(struct AppContext* ctx)
{
    (void)ctx;
    SM_Logs(LOG_DEBUG, _XFAPI_,
            "[OCUDU_BRIDGE] L2 init no-op (handled by XSM_OpenDual in L1 init).");
    return 0;
}

static void ocudu_l2_destroy(struct AppContext* ctx)
{
    (void)ctx;
    SM_Logs(LOG_DEBUG, _XFAPI_,
            "[OCUDU_BRIDGE] L2 destroy no-op (handled in L1 destroy).");
}

static const OCUDU_L2_Interface g_ocudu_l2_interface = {
    .init     = ocudu_l2_init,
    .send_msg = NULL,        /* Sends originate from the forwarder threads */
    .destroy  = ocudu_l2_destroy,
};

const OCUDU_L2_Interface* get_ocudu_l2_interface(void)
{
    return &g_ocudu_l2_interface;
}

#endif /* OCUDU_OCUDU */
