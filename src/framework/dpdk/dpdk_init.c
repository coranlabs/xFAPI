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
#include "rte_memzone.h"
#include <rte_eal.h>

#if defined(OCUDU_OCUDU) || defined(OAI_OCUDU)

uint8_t dpdk_init_ocudu_bridge(xFAPI_Config *g_config)
{
    SM_Logs(LOG_DEBUG, _XFAPI_,
            "[OCUDU_BRIDGE] DPDK file-prefix: %s",
            g_config->dpdk_config.dpdk_memory_zone);

    char *const file_prefix = g_config->dpdk_config.dpdk_memory_zone;
    char iova_mode[64];

    if (g_config->dpdk_config.dpdk_iova_mode == 0)
        snprintf(iova_mode, sizeof(iova_mode), "--iova-mode=pa");
    else
        snprintf(iova_mode, sizeof(iova_mode), "--iova-mode=va");

    /* In OCUDU_OCUDU, OCUDU-L1 is the DPDK PRIMARY and owns the memzone;
     * xFAPI attaches as SECONDARY. In OAI_OCUDU there is no OCUDU-L1 (OAI is
     * on UDP), so xFAPI itself creates the memzone as PRIMARY; OCUDU-L2
     * then attaches as SECONDARY. */
#ifdef OAI_OCUDU
    const char *proc_type = "--proc-type=primary";
    const char *role_label = "PRIMARY";
#else
    const char *proc_type = "--proc-type=secondary";
    const char *role_label = "SECONDARY";
#endif

    char *argv[] = {
        g_config->app_name,
        (char *)proc_type,
        "--file-prefix", file_prefix,
        "--no-pci",
        iova_mode,
    };
    int argc = (int)RTE_DIM(argv);

    SM_Logs(LOG_INFO, _XFAPI_, "[OCUDU_BRIDGE] DPDK EAL args:");
    for (int i = 1; i < argc; i++) {
        SM_Logs(LOG_INFO, _XFAPI_, "  %s", argv[i]);
    }

    if (rte_eal_init(argc, argv) < 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_BRIDGE] rte_eal_init failed (proc-type=%s, file-prefix=%s).",
                role_label, file_prefix);
        return FAILURE;
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_BRIDGE] DPDK EAL attached as %s on file-prefix=%s.",
            role_label, file_prefix);
    return SUCCESS;
}
#endif
