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

static void app_select_interfaces(AppContext* ctx) {
}

int app_init(AppContext* ctx) {
    app_select_interfaces(ctx);

}

void app_run(AppContext* ctx) {
    ctx->is_running = 1;
    SM_Logs(LOG_INFO, _XFAPI_, GREEN "🚀 Application running" RESET_COLOR);
    while(ctx->is_running) {

        sleep(1);
    }
}

void app_destroy(AppContext* ctx) {
    SM_Logs(LOG_INFO, _XFAPI_, "Application cleanup complete.");
}
