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

#include "common_types.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int core_id;
    int priority;
    char sched_policy[16];
} task_config_t;

typedef struct {
    task_config_t l1_to_l2;
    task_config_t l2_to_l1;
} forwarder_config_t;

typedef struct {
    char *device_name;
    uint64_t memory_size;
    int queue_capacity;
    int return_queue_capacity;
} xsm_endpoint_config_t;

typedef struct {
    int  dpdk_iova_mode;
    char dpdk_memory_zone[64];
    char dpdk_device_1[32];
    char dpdk_device_2[32];
} dpdk_config_t;

typedef struct {
    int    mode;
} simulation_mode_t;

typedef struct {
    bool generate_log_file;
    char log_file_path[256];
    bool horizontal_level;
} log_file_config_t;

typedef struct {
    char     vertical_level[16];
    bool     print_config;
    bool     print_datetime;
    log_file_config_t log_file;
} logging_config_t;

typedef struct {
    char          *app_name;
    dpdk_config_t  dpdk_config;
    logging_config_t logging;
    simulation_mode_t simulation_mode;
    xsm_endpoint_config_t xsm_l1;
    xsm_endpoint_config_t xsm_l2;
    forwarder_config_t    forwarder;
} xFAPI_Config;

typedef struct {
    int dpdk_config;
    int logging;
} xFAPI_ConfigFlags;

#define _XFAPI_ "XFAPI"

#endif /* COMMON_GLOBAL_H */
