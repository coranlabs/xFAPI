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

#include "bridge_forwarder.h"
#include "unified_logger.h"
#include <pthread.h>
#include <stdatomic.h>

static atomic_int g_running = 0;
static pthread_t  g_thread_l1_to_l2;
static pthread_t  g_thread_l2_to_l1;

static void *fwd_l1_to_l2(void *arg)
{
    (void)arg;
    SM_Logs(LOG_INFO, _XFAPI_, "[BRIDGE] L1->L2 forwarder thread started.");
    while (atomic_load(&g_running)) {
        /* Forward messages from L1 to L2. */
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[BRIDGE] L1->L2 forwarder thread exiting.");
    return NULL;
}

static void *fwd_l2_to_l1(void *arg)
{
    (void)arg;
    SM_Logs(LOG_INFO, _XFAPI_, "[BRIDGE] L2->L1 forwarder thread started.");
    while (atomic_load(&g_running)) {
        /* Forward messages from L2 to L1. */
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[BRIDGE] L2->L1 forwarder thread exiting.");
    return NULL;
}

int bridge_forwarder_start(AppContext *ctx)
{
    atomic_store(&g_running, 1);
    pthread_create(&g_thread_l1_to_l2, NULL, fwd_l1_to_l2, ctx);
    pthread_create(&g_thread_l2_to_l1, NULL, fwd_l2_to_l1, ctx);
    SM_Logs(LOG_INFO, _XFAPI_, "[BRIDGE] Forwarder threads launched.");
    return 0;
}

void bridge_forwarder_stop(AppContext *ctx)
{
    (void)ctx;
    atomic_store(&g_running, 0);
    pthread_join(g_thread_l1_to_l2, NULL);
    pthread_join(g_thread_l2_to_l1, NULL);
}
