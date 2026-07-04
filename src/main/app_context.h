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
#ifdef OAI_OCUDU
#include "../framework/oai_l1_interface.h"
#endif
#ifdef AERIAL_OCUDU
#include "../framework/aerial_l1_interface.h"
#endif
#if defined(OCUDU_OCUDU) || defined(OAI_OCUDU) || defined(AERIAL_OCUDU)
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

#ifdef OAI_OCUDU

/* Opaque VNF state, defined in core/OAI_OCUDU/oai_vnf.h. Forward-declared
 * here so AppContext can hold a pointer without pulling nFAPI headers into
 * every translation unit that includes app_context.h. */
struct oai_vnf;

typedef struct {
    /* xSM handle toward OCUDU-L2 (pair 1). xFAPI owns the memzone (the role
     * OCUDU-L1 plays in OCUDU_OCUDU) and is SLAVE on pair 1; OCUDU-L2
     * attaches as MASTER on pair 1. */
    xsm_handle_t* h_l2;
    void*         region_l2;

    /* nFAPI VNF runtime state (P5 SCTP + P7 UDP + handshake + threads). */
    struct oai_vnf* vnf;

    /* Queue handing received P7 RX items from the P7 listener thread to the
     * rx_task thread. Carries oai_p7_rx_item_t*. */
    ITC_Queue_t   p7_rx_queue;

    /* L2->OAI forwarder thread: drains the OCUDU-L2 xSM queue and sends the
     * resulting nFAPI message to the PNF. */
    pthread_t     fwd_l2_to_oai_tid;
    atomic_int    fwd_l2_to_oai_running;

    /* Background thread that waits (observability only) for OCUDU-L2 to
     * attach on pair 1, so the VNF can come up without gating on L2. */
    pthread_t     l2_peer_wait_tid;
    int           l2_peer_wait_started;
} OAIOCUDUContext;
#endif

#ifdef AERIAL_OCUDU

/* Opaque nvIPC client state, defined in core/AERIAL_OCUDU/aerial_nvipc.h.
 * Forward-declared here so AppContext can hold a pointer without pulling the
 * nvIPC headers into every translation unit that includes app_context.h. */
struct aerial_nvipc;

typedef struct {
    /* xSM handle toward OCUDU-L2 (pair 1). xFAPI owns the memzone (the role
     * OCUDU-L1 plays in OCUDU_OCUDU) and is SLAVE on pair 1; OCUDU-L2
     * attaches as MASTER on pair 1. */
    xsm_handle_t* h_l2;
    void*         region_l2;

    /* nvIPC secondary runtime state (attach + Aerial->L2 RX loop + TX). */
    struct aerial_nvipc* nvipc;

    /* Cell state latched from CONFIG.request. SLOT.indication needs mu to
     * rebuild OCUDU's slot_point; OCUDU's UL PRACH PDU carries no pci. */
    int           cell_id;
    int           cell_numerology;
    int           cell_pci;
    int           cell_dl_grid;

    /* L2->Aerial forwarder thread: drains the OCUDU-L2 xSM queue and (later)
     * translates + sends each message to Aerial over nvIPC. */
    pthread_t     fwd_l2_to_aerial_tid;
    atomic_int    fwd_l2_to_aerial_running;

    /* Background thread that waits (observability only) for OCUDU-L2 to
     * attach on pair 1, so the nvIPC client can come up without gating on L2. */
    pthread_t     l2_peer_wait_tid;
    int           l2_peer_wait_started;
} AERIALOCUDUContext;
#endif

typedef struct AppContext {
    xFAPI_Config config;
    xFAPI_ConfigFlags config_flags;

#ifdef OCUDU_OCUDU

    OCUDUContext ocudu_ctx;
    const OCUDU_L1_Interface* ocudu_l1_ctx;
    const OCUDU_L2_Interface* ocudu_l2_ctx;
#endif

#ifdef OAI_OCUDU
    OAIOCUDUContext           oai_ocudu_ctx;
    const OAI_L1_Interface*   oai_l1_ctx;
    const OCUDU_L2_Interface* ocudu_l2_ctx;   /* L2 is still OCUDU; reuse vtable */
#endif

#ifdef AERIAL_OCUDU
    AERIALOCUDUContext        aerial_ocudu_ctx;
    const AERIAL_L1_Interface* aerial_l1_ctx;
    const OCUDU_L2_Interface* ocudu_l2_ctx;   /* L2 is still OCUDU; reuse vtable */
#endif

    int is_running;
} AppContext;

#endif
