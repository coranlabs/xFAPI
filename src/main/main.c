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


#include "../include/common_global.h"
#include "../utils/config/yaml_config.h"
#include "../utils/config/yaml_fail_check.h"
#include "../sim/common/sim_common.h"
#include "../framework/framework_init.h"
#include "../framework/dpdk/dpdk_init.h"
#include "app_context.h"
#include "startup_banner.h"
#include "../utils/stats_generation/message_stats.h"
#include <signal.h>
#include <string.h>
#include <stdlib.h>

static AppContext* g_app_context_for_cleanup = NULL;

void perform_app_cleanup(xFAPI_Config* config) {
    SM_Logs(LOG_INFO, _XFAPI_, "Performing cleanup before exit...");

    SM_Logs(LOG_INFO, _XFAPI_, "Dumping message statistics...");
    dump_message_stats_to_json();

    if (config->log_file.generate_log_file) {
        save_logs_to_file("generated_logs/xfapi_logs.txt");
    } else {
        SM_Logs(LOG_INFO, _XFAPI_, "Log file generation is disabled.");
    }

    SM_Logs(LOG_INFO, _XFAPI_, "Cleanup complete.");
}

static void cleanup_handler_wrapper(void) {

    if (g_app_context_for_cleanup != NULL) {

        perform_app_cleanup(&g_app_context_for_cleanup->config);
    } else {

        printf("ERROR: Cleanup handler called but global context was not set.\n");
    }
}

void handle_signal(int signo) {
    SM_Logs(LOG_WARNING, _XFAPI_, "Caught signal %d, triggering cleanup...", signo);
    cleanup_handler_wrapper();

    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {

    AppContext app_context = {0};
    g_app_context_for_cleanup = &app_context;

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Starting xFAPI...");
    const char *cfg_filename = NULL;
    const char *sim_cfg_filename = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--cfgfile") == 0) {
            if (i + 1 < argc) {
                cfg_filename = argv[++i];
            } else {
                fprintf(stderr, "Error: --cfgfile requires an argument.\n");
                fprintf(stderr, "Usage: %s --cfgfile <path_to_yaml_config> [--simcfg <path_to_sim_config>]\n", argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--simcfg") == 0) {
            if (i + 1 < argc) {
                sim_cfg_filename = argv[++i];
            } else {
                fprintf(stderr, "Error: --simcfg requires an argument.\n");
                fprintf(stderr, "Usage: %s --cfgfile <path_to_yaml_config> [--simcfg <path_to_sim_config>]\n", argv[0]);
                return 1;
            }
        } else {
            fprintf(stderr, "Error: Unknown argument '%s'.\n", argv[i]);
            fprintf(stderr, "Usage: %s --cfgfile <path_to_yaml_config> [--simcfg <path_to_sim_config>]\n", argv[0]);
            return 1;
        }
    }

    if (cfg_filename == NULL) {
        fprintf(stderr, "Error: --cfgfile is mandatory.\n");
        fprintf(stderr, "Usage: %s --cfgfile <path_to_yaml_config> [--simcfg <path_to_sim_config>]\n", argv[0]);
        return 1;
    }

    printf("Using configuration file: %s\n", cfg_filename);

    app_context.config.app_name = strdup(argv[0]);
    if (app_context.config.app_name == NULL) {
        fprintf(stderr, "Failed to allocate memory for app_name.\n");
        return 1;
    }
    printf("APP Name: %s \n", app_context.config.app_name);

    if (parse_yaml_main(cfg_filename, &app_context) != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "Aborting: main YAML config '%s' could not be fully parsed.",
                cfg_filename);
        free(app_context.config.app_name);
        return 1;
    }

    validate_and_fill_config(&app_context.config, &app_context.config_flags);

    print_config_table(&app_context.config);

    if(get_current_sim_mode(&app_context.config) != SIM_MODE_DISABLED) {
        SM_Logs(LOG_INFO, _XFAPI_, "Simulation mode is enabled.");

        if (sim_cfg_filename != NULL) {
            SM_Logs(LOG_INFO, _XFAPI_, "Using simulation configuration file: %s", sim_cfg_filename);
            if (parse_yaml_sim_mode(sim_cfg_filename,
                                    &app_context.config,
                                    &app_context.config_flags) != 0) {
                SM_Logs(LOG_ERROR, _XFAPI_,
                        "Aborting: sim YAML config '%s' could not be fully parsed.",
                        sim_cfg_filename);
                free(app_context.config.app_name);
                return 1;
            }
            validate_and_fill_sim_config(&app_context.config, &app_context.config_flags);

            print_sim_config_table(&app_context.config);

        }
        else {
            SM_Logs(LOG_CRTERR, _XFAPI_, "Simulation mode set to %d, but no simulation configuration file provided. Exiting.",
                    app_context.config.simulation_mode.mode);
        }
    }

    logger_init(&app_context.config);

    XFAPI_LOG_BANNER();

    SM_Logs(LOG_INFO, _XFAPI_, "[INIT] message statistics ready");
    init_message_stats();

    SM_Logs(LOG_INFO, _XFAPI_, "[INIT] config loaded: %s", cfg_filename);
    SM_Logs(LOG_INFO, _XFAPI_, YELLOW "[INIT] initializing application" RESET_COLOR);
    if (app_init(&app_context) != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "Application initialization failed!");
        return 1;
    }

    app_run(&app_context);

    app_destroy(&app_context);

    SM_Logs(LOG_INFO,_XFAPI_, "Shutdown complete.");
    return 0;
}
