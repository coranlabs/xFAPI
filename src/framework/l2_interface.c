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

#include "l2_interface.h"
#include "unified_logger.h"

static int l2_init(xFAPI_Config *config, void *ctx)
{
    (void)config; (void)ctx;
    SM_Logs(LOG_INFO, _XFAPI_, "L2 interface initialised.");
    return 0;
}

static int l2_send(void *ctx, void *msg, int len)
{
    (void)ctx; (void)msg; (void)len;
    return 0;
}

static int l2_recv(void *ctx, void *msg, int len)
{
    (void)ctx; (void)msg; (void)len;
    return 0;
}

static void l2_destroy(void *ctx)
{
    (void)ctx;
}

static const L2_Interface g_l2_iface = {
    .init    = l2_init,
    .send    = l2_send,
    .recv    = l2_recv,
    .destroy = l2_destroy,
};

const L2_Interface *get_l2_interface(void) { return &g_l2_iface; }
