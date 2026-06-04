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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yaml_fail_check.h"

bool is_valid_core_id(int core_id) {
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    return (core_id >= 0 && core_id < num_cores);
}

void report_missing_critical_param(const char *param_name) {
    SM_Logs(LOG_CRTERR, _XFAPI_, "Missing critical config: %s\n", param_name);
}

#ifdef OCUDU_OCUDU

void validate_and_fill_config(xFAPI_Config *config, xFAPI_ConfigFlags *flags) {
    (void)flags;

    if (config->ocudu_xsm_l1.device_name[0] == '\0') {
        report_missing_critical_param("ocudu_xsm_l1.device_name");
    }
    if (config->ocudu_xsm_l1.memory_size == 0) {
        report_missing_critical_param("ocudu_xsm_l1.memory_size");
    }
    if (config->ocudu_xsm_l2.device_name[0] == '\0') {
        report_missing_critical_param("ocudu_xsm_l2.device_name");
    }
    if (config->ocudu_xsm_l2.memory_size == 0) {
        report_missing_critical_param("ocudu_xsm_l2.memory_size");
    }
    if (config->ocudu_forwarder.sched_policy[0] == '\0') {
        strncpy(config->ocudu_forwarder.sched_policy, "SCHED_OTHER",
                sizeof(config->ocudu_forwarder.sched_policy) - 1);
    }
    if (config->ocudu_forwarder.priority < 0) {
        config->ocudu_forwarder.priority = 0;
    }
    if (config->dpdk_config.dpdk_memory_zone[0] == '\0') {
        report_missing_critical_param("dpdk_config.dpdk_memory_zone");
    }
}

void print_config_table(const xFAPI_Config *config) {
    SM_Logs(LOG_INFO, _XFAPI_, "===== OCUDU_OCUDU configuration =====");
    SM_Logs(LOG_INFO, _XFAPI_, "DPDK file-prefix:       %s",
            config->dpdk_config.dpdk_memory_zone);
    SM_Logs(LOG_INFO, _XFAPI_, "DPDK iova-mode:         %s",
            config->dpdk_config.dpdk_iova_mode == 0 ? "PA" : "VA");
    SM_Logs(LOG_INFO, _XFAPI_, "xSM L1 memzone:         %s (%lu bytes)",
            config->ocudu_xsm_l1.device_name,
            (unsigned long)config->ocudu_xsm_l1.memory_size);
    SM_Logs(LOG_INFO, _XFAPI_, "xSM L2 memzone:         %s (%lu bytes)",
            config->ocudu_xsm_l2.device_name,
            (unsigned long)config->ocudu_xsm_l2.memory_size);
    SM_Logs(LOG_INFO, _XFAPI_, "Fwd L1->L2 core:        %d",
            config->ocudu_forwarder.l1_to_l2_core_id);
    SM_Logs(LOG_INFO, _XFAPI_, "Fwd L2->L1 core:        %d",
            config->ocudu_forwarder.l2_to_l1_core_id);
    SM_Logs(LOG_INFO, _XFAPI_, "Fwd priority/policy:    %d / %s",
            config->ocudu_forwarder.priority,
            config->ocudu_forwarder.sched_policy);
    SM_Logs(LOG_INFO, _XFAPI_, "=====================================");
}

#endif

#ifdef OAI_OCUDU

void validate_and_fill_config(xFAPI_Config *config, xFAPI_ConfigFlags *flags) {
    (void)flags;

    if (config->ocudu_xsm_l2.device_name[0] == '\0') {
        report_missing_critical_param("ocudu_xsm_l2.device_name");
    }
    if (config->ocudu_xsm_l2.memory_size == 0) {
        report_missing_critical_param("ocudu_xsm_l2.memory_size");
    }
    if (config->nfapi_socket.local_ip[0] == '\0') {
        report_missing_critical_param("nfapi_socket.local_ip");
    }
    if (config->nfapi_socket.remote_ip[0] == '\0') {
        report_missing_critical_param("nfapi_socket.remote_ip");
    }
    if (config->nfapi_socket.p7_local_port == 0) {
        report_missing_critical_param("nfapi_socket.p7_local_port");
    }
    if (config->oai_forwarder.sched_policy[0] == '\0') {
        strncpy(config->oai_forwarder.sched_policy, "SCHED_OTHER",
                sizeof(config->oai_forwarder.sched_policy) - 1);
    }
    if (config->oai_forwarder.priority < 0) {
        config->oai_forwarder.priority = 0;
    }
    if (config->dpdk_config.dpdk_memory_zone[0] == '\0') {
        report_missing_critical_param("dpdk_config.dpdk_memory_zone");
    }
}

void print_config_table(const xFAPI_Config *config) {
    SM_Logs(LOG_INFO, _XFAPI_, "===== OAI_OCUDU configuration =====");
    SM_Logs(LOG_INFO, _XFAPI_, "DPDK file-prefix:       %s",
            config->dpdk_config.dpdk_memory_zone);
    SM_Logs(LOG_INFO, _XFAPI_, "DPDK iova-mode:         %s",
            config->dpdk_config.dpdk_iova_mode == 0 ? "PA" : "VA");
    SM_Logs(LOG_INFO, _XFAPI_, "xSM L2 memzone:         %s (%lu bytes)",
            config->ocudu_xsm_l2.device_name,
            (unsigned long)config->ocudu_xsm_l2.memory_size);
    SM_Logs(LOG_INFO, _XFAPI_, "nFAPI local  ip:        %s", config->nfapi_socket.local_ip);
    SM_Logs(LOG_INFO, _XFAPI_, "nFAPI remote ip:        %s", config->nfapi_socket.remote_ip);
    SM_Logs(LOG_INFO, _XFAPI_, "nFAPI P5 local/remote:  %d / %d",
            config->nfapi_socket.p5_local_port, config->nfapi_socket.p5_remote_port);
    SM_Logs(LOG_INFO, _XFAPI_, "nFAPI P7 local/remote:  %d / %d",
            config->nfapi_socket.p7_local_port, config->nfapi_socket.p7_remote_port);
    SM_Logs(LOG_INFO, _XFAPI_, "Fwd recv/send core:     %d / %d",
            config->oai_forwarder.recv_core_id, config->oai_forwarder.send_core_id);
    SM_Logs(LOG_INFO, _XFAPI_, "Fwd priority/policy:    %d / %s",
            config->oai_forwarder.priority, config->oai_forwarder.sched_policy);
    SM_Logs(LOG_INFO, _XFAPI_, "===================================");
}

#endif

void validate_and_fill_sim_config(xFAPI_Config *config, xFAPI_ConfigFlags *flags) {

    if (!flags->simulation_mode.core_config.rx_task.core_id){
        SM_Logs(LOG_WARN, _XFAPI_, "Sim mode, RX Task Core id not specified will use dynamically.");
    }
    else{
        if (is_valid_core_id(config->simulation_mode.core_config.rx_task.core_id) == false) {
            SM_Logs(LOG_CRTERR, _XFAPI_, "Invalid core_id %d for Sim mode RX Task. Must be between 0 and %ld.",
                    config->simulation_mode.core_config.rx_task.core_id, sysconf(_SC_NPROCESSORS_ONLN) - 1);
        }
    }

    if (!flags->simulation_mode.core_config.rx_task.priority){
        SM_Logs(LOG_WARN, _XFAPI_, "Sim mode, RX Task priority not specified, defaulting to 80.");
        config->simulation_mode.core_config.rx_task.priority = 80;
    }
    if (!flags->simulation_mode.core_config.rx_task.sched_policy){
        SM_Logs(LOG_WARN, _XFAPI_, "Sim mode, RX Task scheduling policy not specified, defaulting to SCHED_FIFO.");
        strncpy(config->simulation_mode.core_config.rx_task.sched_policy, "SCHED_FIFO", sizeof(config->simulation_mode.core_config.rx_task.sched_policy) - 1);
        config->simulation_mode.core_config.rx_task.sched_policy[sizeof(config->simulation_mode.core_config.rx_task.sched_policy) - 1] = '\0';
    }

    if (!flags->simulation_mode.core_config.tx_task.core_id){
        SM_Logs(LOG_WARN, _XFAPI_, "Sim mode, TX Task Core id not specified will use dynamically.");
    }
    else{
        if (is_valid_core_id(config->simulation_mode.core_config.tx_task.core_id) == false) {
            SM_Logs(LOG_CRTERR, _XFAPI_, "Invalid core_id %d for Sim mode TX Task. Must be between 0 and %ld.",
                    config->simulation_mode.core_config.tx_task.core_id, sysconf(_SC_NPROCESSORS_ONLN) - 1);
        }
    }
    if (!flags->simulation_mode.core_config.tx_task.priority){
        SM_Logs(LOG_WARN, _XFAPI_, "Sim mode, TX Task priority not specified, defaulting to 80.");
        config->simulation_mode.core_config.tx_task.priority = 80;
    }
    if (!flags->simulation_mode.core_config.tx_task.sched_policy){
        SM_Logs(LOG_WARN, _XFAPI_, "Sim mode, TX Task scheduling policy not specified, defaulting to SCHED_FIFO.");
        strncpy(config->simulation_mode.core_config.tx_task.sched_policy, "SCHED_FIFO", sizeof(config->simulation_mode.core_config.tx_task.sched_policy) - 1);
        config->simulation_mode.core_config.tx_task.sched_policy[sizeof(config->simulation_mode.core_config.tx_task.sched_policy) - 1] = '\0';
    }

}

void print_sim_config_table(const xFAPI_Config *config) {
    char buffer[CONFIG_TABLE_BUFFER_SIZE];
    size_t offset = 0;

    #define APPEND_LINE(...) offset += snprintf(buffer + offset, sizeof(buffer) - offset, __VA_ARGS__)
    #define BOOL_STR(b) ((b) ? "enabled" : "disabled")

    APPEND_LINE("\n");
    APPEND_LINE("|----------------------------------------------------------------------------|\n");
    APPEND_LINE("| %-41s | %-30s |\n", "Simulator configs  :", "Value");
    APPEND_LINE("|----------------------------------------------------------------------------|\n");
    APPEND_LINE("| %-74s |\n", "-CORE CONFIG :");
    APPEND_LINE("|----------------------------------------------------------------------------|\n");

    #define PRINT_TASK(task_name, task) \
        APPEND_LINE("| %-74s |\n", "-" task_name " CONFIG :"); \
        APPEND_LINE("| %-41s | %-30d |\n", task_name " - CORE_ID", task.core_id); \
        APPEND_LINE("| %-41s | %-30d |\n", task_name " - PRIORITY", task.priority); \
        APPEND_LINE("| %-41s | %-30s |\n", task_name " - SCHED_POLICY", task.sched_policy)

    PRINT_TASK("RX_TASK", config->simulation_mode.core_config.rx_task);
    PRINT_TASK("TX_TASK", config->simulation_mode.core_config.tx_task);

    APPEND_LINE("|----------------------------------------------------------------------------|\n");

    if (config->log_file.generate_log_file && config->log_file.print_config) {
        char buffer_copy[coranLabs_MAX_TEXT_LENGTH];
        strncpy(buffer_copy, buffer, sizeof(buffer_copy) - 1);
        buffer_copy[sizeof(buffer_copy) - 1] = '\0';

        Display_file_big(LOG_INFO, _XFAPI_, buffer_copy,config->log_file.print_datetime);
    }

    if (config->logging.print_config) {
        Display(LOG_INFO, _XFAPI_, buffer, config->logging.print_datetime);
    }

    #undef APPEND_LINE
    #undef BOOL_STR
    #undef PRINT_TASK
}
