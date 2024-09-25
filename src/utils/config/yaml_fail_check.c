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
