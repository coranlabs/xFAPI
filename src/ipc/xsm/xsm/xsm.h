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

#ifndef OCUDU_XSM_XSM_XSM_H
#define OCUDU_XSM_XSM_XSM_H

#include <stddef.h>
#include <stdint.h>

struct rte_mempool;

#ifdef __cplusplus
extern "C" {
#endif

#define XSM_VERSION_MAJOR 1
#define XSM_VERSION_MINOR 0
#define XSM_VERSION_PATCH 0

#define XSM_PROTOCOL_MAGIC   0x0058534D31303030ULL
#define XSM_PROTOCOL_VERSION 3

#define XSM_DEVICE_NAME_MAX 32
#define XSM_MAX_HUGEPAGES   32

typedef enum xsm_status {
    XSM_OK                       = 0,
    XSM_ERR_INVALID_HANDLE       = -1,
    XSM_ERR_INVALID_ARGUMENT     = -2,
    XSM_ERR_NO_MEMORY            = -3,
    XSM_ERR_DPDK_FAILURE         = -4,
    XSM_ERR_PEER_NOT_READY       = -5,
    XSM_ERR_QUEUE_FULL           = -6,
    XSM_ERR_QUEUE_EMPTY          = -7,
    XSM_ERR_PROTOCOL_MISMATCH    = -8,
    XSM_ERR_HUGEPAGE_FAILURE     = -9,
    XSM_ERR_BUFFER_OVERFLOW      = -10,
    XSM_ERR_NOT_INITIALIZED      = -11,
    XSM_ERR_ALREADY_INITIALIZED  = -12,
    XSM_ERR_INTERNAL             = -99
} xsm_status_t;

const char *xsm_strerror(xsm_status_t status);

typedef enum xsm_role {
    XSM_ROLE_SLAVE  = 0,
    XSM_ROLE_MASTER = 1
} xsm_role_t;

typedef enum xsm_wakeup_mode {
    XSM_WAKEUP_POSIX_SEM = 0
} xsm_wakeup_mode_t;

typedef enum xsm_transport {
    XSM_TRANSPORT_MEMZONE  = 0,
    XSM_TRANSPORT_DPDK_ETH = 1
} xsm_transport_t;

typedef struct xsm_eth_params {
    uint16_t     local_port_id;
    uint8_t      peer_mac[6];
    uint16_t     mtu;
    int32_t      rx_lcore_id;
    char         local_memzone_name[XSM_DEVICE_NAME_MAX];
    uint64_t     local_memzone_size;
    uint8_t      attach_existing;
    uint8_t      _pad_attach[7];
    uint64_t     reserved[4];
} xsm_eth_params_t;

typedef struct xsm_config {
    char              device_name[XSM_DEVICE_NAME_MAX];
    xsm_role_t        role;
    uint64_t          memory_size;
    uint32_t          queue_capacity;
    uint32_t          return_queue_capacity;
    xsm_wakeup_mode_t wakeup_mode;
    int32_t           numa_hint;
    uint32_t          pair_index;
    uint32_t          num_pairs;
    uint64_t          reserved[4];
    xsm_transport_t   transport;
    xsm_eth_params_t  eth;
} xsm_config_t;

#define XSM_MAX_PAIRS_PUBLIC 2

typedef struct xsm_handle xsm_handle_t;

typedef struct xsm_msg {
    uint64_t payload_pa;
    uint32_t payload_size;
    uint16_t type_id;
    uint16_t flags;
} xsm_msg_t;

typedef struct xsm_stats {
    uint64_t tx_msgs;
    uint64_t tx_full_drops;
    uint64_t rx_msgs;
    uint64_t wait_count;
    uint64_t notify_count;
    uint64_t buffers_returned;
    uint64_t buffers_acquired;
    uint64_t tx_alloc_fail_drops;
    uint64_t tx_burst_short_drops;
} xsm_stats_t;

uint32_t     XSM_GetVersion(void);

xsm_status_t XSM_Open(const xsm_config_t *cfg, xsm_handle_t **handle);
xsm_status_t XSM_OpenDual(const xsm_config_t *cfg_a, const xsm_config_t *cfg_b,
                           xsm_handle_t **handle_a, xsm_handle_t **handle_b);
xsm_status_t XSM_Close(xsm_handle_t *handle);
xsm_status_t XSM_IsPeerReady(xsm_handle_t *handle);

xsm_status_t XSM_Alloc(xsm_handle_t *handle, uint64_t size, void **region);
xsm_status_t XSM_Free(xsm_handle_t *handle, void *region);
uint64_t     XSM_VirtToPhys(xsm_handle_t *handle, const void *va);
void        *XSM_PhysToVirt(xsm_handle_t *handle, uint64_t pa);

xsm_status_t XSM_Put(xsm_handle_t *handle, const xsm_msg_t *msg);
xsm_status_t XSM_Get(xsm_handle_t *handle, xsm_msg_t *msg);
uint32_t     XSM_Pending(xsm_handle_t *handle);
xsm_status_t XSM_Wait(xsm_handle_t *handle, uint32_t timeout_ms);
xsm_status_t XSM_Notify(xsm_handle_t *handle);
xsm_status_t XSM_WaitGet(xsm_handle_t *handle, uint32_t timeout_ms, xsm_msg_t *msg);

xsm_status_t XSM_ReturnBuffer(xsm_handle_t *handle, uint64_t buffer_pa);
xsm_status_t XSM_AcquireBuffer(xsm_handle_t *handle, uint64_t *buffer_pa);
uint32_t     XSM_AvailableBuffers(xsm_handle_t *handle);

struct rte_mempool *XSM_GetRxMempool(xsm_handle_t *handle);

xsm_status_t XSM_GetStats(xsm_handle_t *handle, xsm_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* OCUDU_XSM_XSM_XSM_H */
