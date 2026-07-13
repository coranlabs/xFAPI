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

#ifndef COMMON_GLOBAL_H
#define COMMON_GLOBAL_H

#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include "common_types.h"
#include "itc_queue.h"

typedef struct {
    int core_id;
    int priority;
    char sched_policy[20];
} task_config_t;

typedef struct {
    pthread_t thread_id;
    pthread_attr_t thread_attr;
    char name[32];
    void *(*start_routine)(void *);
    void *arg;
    volatile int should_run;
    int core_id;
    int priority;
    ITC_Queue_t queue;

} thread_mgt_t;

typedef struct {
    thread_mgt_t thread_mgt;
    int core_id;
    char sched_policy[20];
    int priority;
} thread_task_t;

typedef struct {
    thread_task_t rx_task;
    thread_task_t tx_task;
    thread_task_t xSM_recv_task;
    thread_task_t dashboard_task;
} core_config_t;

typedef struct {
    thread_task_t rx_task;
    thread_task_t tx_task;
} core_config_sim_mode_t;

typedef struct {
    int dpdk_iova_mode;
    char dpdk_memory_zone[32];
    char dpdk_device_1[32];
    char dpdk_device_2[32];
} dpdk_config_t;

typedef struct {
    char wls_device_name[512];
    unsigned long wls_mem_size;
    uint64_t wls_shmem_size;
} wls_config_t;

#if defined(OCUDU_OCUDU) || defined(OAI_OCUDU) || defined(AERIAL_OCUDU)

typedef struct {
    char     device_name[64];
    uint64_t memory_size;
    uint32_t queue_capacity;
    uint32_t return_queue_capacity;
} ocudu_xsm_endpoint_config_t;

#endif

#ifdef OCUDU_OCUDU

typedef struct {
    int  l1_to_l2_core_id;
    int  l2_to_l1_core_id;
    int  priority;
    char sched_policy[20];
} ocudu_forwarder_config_t;

/* Split-mode configuration. Selected via YAML key `split.role`:
 *   none -> single-host bridge (today's behaviour, both pairs in one memzone)
 *   L1   -> this XFAPI process runs on the L1 server, the L2 side is on a
 *           peer XFAPI reached over a DPDK NIC link
 *   L2   -> symmetric: this XFAPI process runs on the L2 server
 */
typedef enum {
    SPLIT_NONE = 0,
    SPLIT_L1   = 1,
    SPLIT_L2   = 2
} split_role_t;

typedef struct {
    split_role_t role;
    char         local_pci[32];
    char         peer_mac[32];
    uint16_t     mtu;
    int          rx_lcore_id;
    char         local_memzone_name[32];
    uint64_t     local_memzone_size;
} split_config_t;
#endif

#ifdef OAI_OCUDU

/* UDP socket endpoints toward OAI L1 (nFAPI P5/P7). xFAPI binds the
 * local_ip:*_local_port pairs and sends to remote_ip:*_remote_port. */
typedef struct {
    bool ipv6_enabled;
    char remote_ip[64];
    char local_ip[64];
    int  p5_remote_port;
    int  p5_local_port;
    int  p7_remote_port;
    int  p7_local_port;
    bool checksum_enabled;
} nfapi_socket_t;

/* Forwarder thread settings for the OAI_OCUDU bridge. recv = OAI->L2
 * (socket recv -> xSM), send = L2->OAI (xSM -> socket send). */
typedef struct {
    int  recv_core_id;
    int  send_core_id;
    int  priority;
    char sched_policy[20];
} oai_forwarder_config_t;
#endif

#ifdef AERIAL_OCUDU

/* nvIPC client configuration (toward Aerial L1). xFAPI runs as the nvIPC
 * SECONDARY (the role testMAC plays); Aerial cuphycontroller is the nvIPC
 * PRIMARY that owns the shared-memory pools. The secondary only needs the SHM
 * transport prefix — it auto-discovers the primary's pool layout at attach
 * time — so the config is carried inline here (no separate nvIPC YAML). */
typedef struct {
    char prefix[32];         /* SHM prefix; must match Aerial's nvipc prefix  */
    bool blocking;           /* 1 = rx_tti_sem_wait loop, 0 = epoll on get_fd */
} nvipc_config_t;

/* Forwarder thread settings for the AERIAL_OCUDU bridge. recv = Aerial->L2
 * (nvIPC rx -> xSM), send = L2->Aerial (xSM -> nvIPC tx). */
typedef struct {
    int  recv_core_id;
    int  send_core_id;
    int  priority;
    char sched_policy[20];
} aerial_forwarder_config_t;
#endif

#ifdef AERIAL_OAI

/* nvIPC client configuration (toward Aerial L1). xFAPI is the nvIPC SECONDARY;
 * Aerial cuphycontroller is the PRIMARY that owns the SHM pools. The secondary
 * only needs the SHM transport prefix. */
typedef struct {
    char prefix[32];         /* SHM prefix; must match Aerial's nvipc prefix  */
    bool blocking;           /* 1 = rx_tti_sem_wait loop, 0 = epoll on get_fd */
} aerial_oai_nvipc_config_t;

/* nFAPI socket endpoints toward OAI L2 (MAC). OAI's nFAPI VNF is the SCTP
 * SERVER (it listens); xFAPI is the PNF that connects OUT to it. So P5 is a
 * client connect to remote_ip:p5_remote_port (optionally bound to
 * local_ip:p5_local_port). P7 is UDP: xFAPI binds p7_local_port and sends to
 * remote_ip:p7_remote_port. */
typedef struct {
    bool ipv6_enabled;
    char remote_ip[64];      /* OAI L2 (VNF) address                          */
    char local_ip[64];       /* this xFAPI (PNF) address                      */
    int  p5_remote_port;     /* OAI VNF P5 listen port; xFAPI connects here   */
    int  p5_local_port;      /* xFAPI P5 local bind port (source)             */
    int  p7_local_port;      /* xFAPI UDP-binds here                          */
    int  p7_remote_port;     /* OAI L2 P7 dest port                           */
    bool checksum_enabled;
} aerial_oai_nfapi_socket_t;

/* Forwarder/thread core pinning for the AERIAL_OAI bridge. recv = Aerial->OAI
 * (nvIPC rx thread), send = OAI->Aerial (nFAPI socket rx thread). */
typedef struct {
    int  recv_core_id;
    int  send_core_id;
    int  priority;
    char sched_policy[20];
} aerial_oai_forwarder_config_t;
#endif

typedef struct {
    int mode;
    core_config_sim_mode_t core_config;
} simulation_mode_t;

typedef struct {
    bool xFAPI_log;
    bool xSM_log;
    bool P5_log;
    bool P7_log;
} horizontal_log_t;

typedef struct {
    char vertical_level[16];
    horizontal_log_t horizontal_level;
    bool print_config;
    bool print_datetime;
} logging_config_t;

typedef struct {
    bool generate_log_file;
    int file_size;
    char vertical_level[16];
    horizontal_log_t horizontal_level;
    bool print_datetime;
    bool print_config;

} log_file_config_t;

typedef struct {
    bool enabled;
    char bind_ip[64];
    int port;
    int core_id;
} dashboard_config_t;

typedef struct {
    bool generate_summary_file;
    bool generate_detailed_file;
    bool capture_ul_msgs;
    bool capture_dl_msgs;
    bool capture_p5_msgs;
    bool capture_p7_msgs;
} stats_file_generation_t;

typedef struct {
    char *app_name;
    core_config_t core_config;
    dpdk_config_t dpdk_config;
    wls_config_t wls_config;
    simulation_mode_t simulation_mode;
    logging_config_t logging;
    log_file_config_t log_file;
    dashboard_config_t dashboard;
    stats_file_generation_t stats_file_generation;
#ifdef OCUDU_OCUDU
    ocudu_xsm_endpoint_config_t   ocudu_xsm_l1;
    ocudu_xsm_endpoint_config_t   ocudu_xsm_l2;
    ocudu_forwarder_config_t      ocudu_forwarder;
    split_config_t                split;
#endif
#ifdef OAI_OCUDU
    ocudu_xsm_endpoint_config_t   ocudu_xsm_l2;
    nfapi_socket_t                nfapi_socket;
    oai_forwarder_config_t        oai_forwarder;
#endif
#ifdef AERIAL_OCUDU
    ocudu_xsm_endpoint_config_t   ocudu_xsm_l2;
    nvipc_config_t                nvipc;
    aerial_forwarder_config_t     aerial_forwarder;
#endif
#ifdef AERIAL_OAI
    aerial_oai_nvipc_config_t     nvipc;
    aerial_oai_nfapi_socket_t     nfapi_socket;
    aerial_oai_forwarder_config_t forwarder;
#endif
} xFAPI_Config;

typedef struct {
    bool core_id;
    bool priority;
    bool sched_policy;
} task_config_flags_t;

typedef struct {
    task_config_flags_t rx_task;
    task_config_flags_t tx_task;
    task_config_flags_t xSM_recv_task;
    task_config_flags_t dashboard_task;
} core_config_flags_t;

typedef struct {
    task_config_flags_t rx_task;
    task_config_flags_t tx_task;
} core_config_sim_mode_flags_t;

typedef struct {
    bool dpdk_iova_mode;
    bool dpdk_memory_zone;
    bool dpdk_device_1;
    bool dpdk_device_2;
} dpdk_config_flags_t;

typedef struct {
    bool wls_device_name;
    bool wls_mem_size;
} wls_config_flags_t;

typedef struct {
    bool mode;
    core_config_sim_mode_flags_t core_config;
} simulation_mode_flags_t;

typedef struct {
    bool xFAPI_log;
    bool xSM_log;
    bool P5_log;
    bool P7_log;
} horizontal_log_flags_t;

typedef struct {
    bool vertical_level;
    horizontal_log_flags_t horizontal_level;
    bool print_config;
    bool print_datetime;
} logging_config_flags_t;

typedef struct {
    bool generate_log_file;
    bool file_size;
    bool vertical_level;
    horizontal_log_flags_t horizontal_level;
    bool print_datetime;
    bool print_config;
} log_file_config_flags_t;

typedef struct {
    bool enabled;
    bool bind_ip;
    bool port;
    bool core_id;
} dashboard_config_flags_t;

typedef struct {
    bool generate_summary_file;
    bool generate_detailed_file;
    bool capture_ul_msgs;
    bool capture_dl_msgs;
    bool capture_p5_msgs;
    bool capture_p7_msgs;
} stats_file_generation_flags_t;

typedef struct {
    core_config_flags_t core_config;
    dpdk_config_flags_t dpdk_config;
    wls_config_flags_t wls_config;
    simulation_mode_flags_t simulation_mode;
    logging_config_flags_t logging;
    log_file_config_flags_t log_file;
    dashboard_config_flags_t dashboard;
    stats_file_generation_flags_t stats_file_generation;
} xFAPI_ConfigFlags;

#endif
