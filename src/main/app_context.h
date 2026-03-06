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

#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <pthread.h>
#include <netinet/in.h>
#include "unified_logger.h"
#include "../framework/ocudu_l1_interface.h"
#include "../framework/ocudu_l2_interface.h"
#ifdef OCUDU_OCUDU
#include "xsm/xsm.h"
#include <stdbool.h>
#include <stdatomic.h>
#endif

#ifdef OCUDU_OCUDU

typedef struct {
    xsm_handle_t* h_l1;
    xsm_handle_t* h_l2;
    void*         region_l1;
    void*         region_l2;
    pthread_t     fwd_l1_to_l2_tid;
    pthread_t     fwd_l2_to_l1_tid;
    atomic_int    forwarders_running;

    void* pair0_master_pool;

    /* Split-mode state. When config.split.role != SPLIT_NONE, exactly ONE
     * of (h_l1, h_l2) is a memzone handle (toward the local OCUDU side)
     * and h_eth is the DPDK-Ethernet handle to the peer XFAPI on the
     * other host. In SPLIT_NONE mode h_eth stays NULL.
     *
     * split_port_id is the DPDK ethdev port_id of the L1<->L2 NIC link.
     *
     * region_eth_va is the base VA of the local memzone backing the
     * DPDK-Ethernet handle's buffer pool; comes back from XSM_Alloc on
     * h_eth. */
    xsm_handle_t* h_eth;
    uint16_t      split_port_id;
    void*         region_eth_va;

    /* Split-mode forwarder thread ids. */
    pthread_t     fwd_split_to_net_tid;
    pthread_t     fwd_split_from_net_tid;
} OCUDUContext;
#endif

typedef struct AppContext {
    xFAPI_Config config;
    xFAPI_ConfigFlags config_flags;

#ifdef OCUDU_OCUDU

    OCUDUContext ocudu_ctx;
    const OCUDU_L1_Interface* ocudu_l1_ctx;
    const OCUDU_L2_Interface* ocudu_l2_ctx;
#endif

    int is_running;
} AppContext;

#endif
