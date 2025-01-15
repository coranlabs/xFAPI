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

#include "dpdk_init.h"
#include "unified_logger.h"
#include <rte_eal.h>
#include <string.h>

int dpdk_init_bridge(xFAPI_Config *config)
{
    const char *prefix = config->dpdk_config.dpdk_memory_zone;
    SM_Logs(LOG_INFO, _XFAPI_, "[BRIDGE] DPDK file-prefix: %s", prefix);

    char proc_type[]    = "--proc-type=secondary";
    char file_prefix[]  = "--file-prefix";
    char no_pci[]       = "--no-pci";
    char iova[]         = "--iova-mode=pa";
    char prefix_val[64];
    strncpy(prefix_val, prefix, sizeof(prefix_val) - 1);
    prefix_val[sizeof(prefix_val) - 1] = '\0';

    char *eal_argv[] = { "xfapi", proc_type, file_prefix, prefix_val, no_pci, iova };
    int   eal_argc  = 6;

    if (rte_eal_init(eal_argc, eal_argv) < 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[BRIDGE] rte_eal_init failed.");
        return -1;
    }
    SM_Logs(LOG_INFO, _XFAPI_, "[BRIDGE] DPDK EAL attached as SECONDARY on file-prefix=%s.", prefix);
    return 0;
}
