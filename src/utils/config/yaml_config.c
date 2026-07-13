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

#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "yaml_config.h"
#include "yaml_utils.h"
#include "unified_logger.h"
#include "../../main/app_context.h"

void set_task_field(thread_task_t* task_cfg, task_config_flags_t* task_flags,
    const char* current_param_key, const char* val) {
    if (strcmp(current_param_key, "core_id") == 0) {
    task_cfg->core_id = atoi(val);
    task_flags->core_id = 1;
    } else if (strcmp(current_param_key, "priority") == 0) {
    task_cfg->priority = atoi(val);
    task_flags->priority = 1;
    } else if (strcmp(current_param_key, "sched_policy") == 0) {

    strncpy(task_cfg->sched_policy, val, sizeof(task_cfg->sched_policy) - 1);
    task_cfg->sched_policy[sizeof(task_cfg->sched_policy) - 1] = '\0';
    task_flags->sched_policy = 1;
    }

}

void parse_core_config(yaml_parser_t *parser, core_config_t *config, core_config_flags_t *config_flags) {
    yaml_event_t event;
    int done = 0;

    int depth = 0;

    char current_sub_key[64] = "";
    char current_param_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in core_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;

                break;

            case YAML_MAPPING_END_EVENT:
                depth--;

                if (depth == 0) {

                    done = 1;
                } else if (depth == 1) {

                    current_sub_key[0] = '\0';
                    expecting_key = 1;

                } else {

                }
                break;

            case YAML_SCALAR_EVENT:

                if (depth == 1 && expecting_key) {
                    strncpy(current_sub_key, (char *)event.data.scalar.value, sizeof(current_sub_key) - 1);
                    current_sub_key[sizeof(current_sub_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 2 && expecting_key) {
                    strncpy(current_param_key, (char *)event.data.scalar.value, sizeof(current_param_key) - 1);
                    current_param_key[sizeof(current_param_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 2 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;

                    if (strcmp(current_sub_key, "rx_task") == 0) {
                        set_task_field(&config->rx_task, &config_flags->rx_task,
                            current_param_key,val);
                    } else if (strcmp(current_sub_key, "tx_task") == 0) {
                        set_task_field(&config->tx_task, &config_flags->tx_task,
                            current_param_key,val);
                    } else if (strcmp(current_sub_key, "wls_recv_task") == 0) {
                        set_task_field(&config->xSM_recv_task, &config_flags->xSM_recv_task,
                            current_param_key,val);
                    } else {
                        fprintf(stderr, "parse_core_config: Unknown sub_key '%s' encountered for task parameters.\n", current_sub_key);
                    }

                    expecting_key = 1;

                } else {

                    fprintf(stderr, "parse_core_config: Unexpected SCALAR event at depth %d, expecting_key: %d. Value: %s. This might indicate an issue.\n",
                           depth, expecting_key, (char*)event.data.scalar.value);

                }
                break;

            default:

                break;
        }

        yaml_event_delete(&event);
    }

}

void parse_sim_mode_core_config(yaml_parser_t *parser, core_config_sim_mode_t *core_cfg, core_config_sim_mode_flags_t *config_flags) {
    yaml_event_t event;
    int done = 0;

    int depth = 0;

    char current_sub_key[64] = "";
    char current_param_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in core_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;

                break;

            case YAML_MAPPING_END_EVENT:
                depth--;

                if (depth == 0) {

                    done = 1;
                } else if (depth == 1) {

                    current_sub_key[0] = '\0';
                    expecting_key = 1;

                } else {

                }
                break;

            case YAML_SCALAR_EVENT:

                if (depth == 1 && expecting_key) {
                    strncpy(current_sub_key, (char *)event.data.scalar.value, sizeof(current_sub_key) - 1);
                    current_sub_key[sizeof(current_sub_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 2 && expecting_key) {
                    strncpy(current_param_key, (char *)event.data.scalar.value, sizeof(current_param_key) - 1);
                    current_param_key[sizeof(current_param_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 2 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;

                    if (strcmp(current_sub_key, "rx_task") == 0) {
                        set_task_field(&core_cfg->rx_task, &config_flags->rx_task,
                            current_param_key,val);
                    } else if (strcmp(current_sub_key, "tx_task") == 0) {
                        set_task_field(&core_cfg->tx_task,  &config_flags->tx_task,
                            current_param_key,val);
                    } else {
                        fprintf(stderr, "parse_core_config: Unknown sub_key '%s' encountered for task parameters.\n", current_sub_key);
                    }

                    expecting_key = 1;

                } else {

                    fprintf(stderr, "parse_core_config: Unexpected SCALAR event at depth %d, expecting_key: %d. Value: %s. This might indicate an issue.\n",
                           depth, expecting_key, (char*)event.data.scalar.value);

                }
                break;

            default:

                break;
        }

        yaml_event_delete(&event);
    }

}

void parse_dpdk_config(yaml_parser_t *parser, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;

    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in dpdk_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;

            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;

            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;

                    if (strcmp(current_key, "dpdk_iova_mode") == 0) {
                        config->dpdk_config.dpdk_iova_mode = atoi(val);
                        config_flags->dpdk_config.dpdk_iova_mode = 1;
                    } else if (strcmp(current_key, "dpdk_memory_zone") == 0) {
                        strncpy(config->dpdk_config.dpdk_memory_zone, val,
                                sizeof(config->dpdk_config.dpdk_memory_zone) - 1);
                        config->dpdk_config.dpdk_memory_zone[sizeof(config->dpdk_config.dpdk_memory_zone) - 1] = '\0';
                        config_flags->dpdk_config.dpdk_memory_zone = 1;
                    } else if (strcmp(current_key, "dpdk_device_1") == 0) {
                        strncpy(config->dpdk_config.dpdk_device_1, val,
                                sizeof(config->dpdk_config.dpdk_device_1) - 1);
                        config->dpdk_config.dpdk_device_1[sizeof(config->dpdk_config.dpdk_device_1) - 1] = '\0';
                        config_flags->dpdk_config.dpdk_device_1 = 1;
                    } else if (strcmp(current_key, "dpdk_device_2") == 0) {
                        strncpy(config->dpdk_config.dpdk_device_2, val,
                                sizeof(config->dpdk_config.dpdk_device_2) - 1);
                        config->dpdk_config.dpdk_device_2[sizeof(config->dpdk_config.dpdk_device_2) - 1] = '\0';
                        config_flags->dpdk_config.dpdk_device_2 = 1;
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,"parse_dpdk_config: Unknown key '%s' in dpdk_config. This might indicate an issue with the YAML structure.",current_key);
                    }

                    expecting_key = 1;
                }
                break;

            default:
                break;
        }

        yaml_event_delete(&event);
    }
}

void parse_wls_config(yaml_parser_t *parser, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in wls_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "wls_device_name") == 0) {
                        strncpy(config->wls_config.wls_device_name, val, sizeof(config->wls_config.wls_device_name) - 1);
                        config->wls_config.wls_device_name[sizeof(config->wls_config.wls_device_name) - 1] = '\0';
                        config_flags->wls_config.wls_device_name = 1;
                    } else if (strcmp(current_key, "wls_mem_size") == 0) {
                        config->wls_config.wls_mem_size = strtoul(val, NULL, 10);
                        config_flags->wls_config.wls_mem_size = 1;
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

#if defined(OCUDU_OCUDU) || defined(OAI_OCUDU) || defined(AERIAL_OCUDU)

static void parse_ocudu_xsm_endpoint_config(yaml_parser_t *parser,
                                            ocudu_xsm_endpoint_config_t *ep) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in ocudu_xsm endpoint\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "device_name") == 0) {
                        strncpy(ep->device_name, val, sizeof(ep->device_name) - 1);
                        ep->device_name[sizeof(ep->device_name) - 1] = '\0';
                    } else if (strcmp(current_key, "memory_size") == 0) {
                        ep->memory_size = strtoull(val, NULL, 10);
                    } else if (strcmp(current_key, "queue_capacity") == 0) {
                        ep->queue_capacity = (uint32_t)strtoul(val, NULL, 10);
                    } else if (strcmp(current_key, "return_queue_capacity") == 0) {
                        ep->return_queue_capacity = (uint32_t)strtoul(val, NULL, 10);
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_ocudu_xsm_endpoint_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

#endif /* OCUDU_OCUDU || OAI_OCUDU || AERIAL_OCUDU */

#ifdef OCUDU_OCUDU

void parse_ocudu_xsm_l1_config(yaml_parser_t *parser, xFAPI_Config *config,
                                xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    parse_ocudu_xsm_endpoint_config(parser, &config->ocudu_xsm_l1);
}

void parse_ocudu_xsm_l2_config(yaml_parser_t *parser, xFAPI_Config *config,
                                xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    parse_ocudu_xsm_endpoint_config(parser, &config->ocudu_xsm_l2);
}

void parse_ocudu_forwarder_config(yaml_parser_t *parser, xFAPI_Config *config,
                                   xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in ocudu_forwarder\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "l1_to_l2_core_id") == 0) {
                        config->ocudu_forwarder.l1_to_l2_core_id = atoi(val);
                    } else if (strcmp(current_key, "l2_to_l1_core_id") == 0) {
                        config->ocudu_forwarder.l2_to_l1_core_id = atoi(val);
                    } else if (strcmp(current_key, "priority") == 0) {
                        config->ocudu_forwarder.priority = atoi(val);
                    } else if (strcmp(current_key, "sched_policy") == 0) {
                        strncpy(config->ocudu_forwarder.sched_policy, val,
                                sizeof(config->ocudu_forwarder.sched_policy) - 1);
                        config->ocudu_forwarder.sched_policy[
                            sizeof(config->ocudu_forwarder.sched_policy) - 1] = '\0';
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_ocudu_forwarder_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}
#endif

#ifdef OAI_OCUDU

void parse_ocudu_xsm_l2_config(yaml_parser_t *parser, xFAPI_Config *config,
                                xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    parse_ocudu_xsm_endpoint_config(parser, &config->ocudu_xsm_l2);
}

void parse_nfapi_socket_config(yaml_parser_t *parser, xFAPI_Config *config,
                               xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in nfapi_socket\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "remote_ip") == 0) {
                        strncpy(config->nfapi_socket.remote_ip, val,
                                sizeof(config->nfapi_socket.remote_ip) - 1);
                        config->nfapi_socket.remote_ip[sizeof(config->nfapi_socket.remote_ip) - 1] = '\0';
                    } else if (strcmp(current_key, "local_ip") == 0) {
                        strncpy(config->nfapi_socket.local_ip, val,
                                sizeof(config->nfapi_socket.local_ip) - 1);
                        config->nfapi_socket.local_ip[sizeof(config->nfapi_socket.local_ip) - 1] = '\0';
                    } else if (strcmp(current_key, "p5_remote_port") == 0) {
                        config->nfapi_socket.p5_remote_port = atoi(val);
                    } else if (strcmp(current_key, "p5_local_port") == 0) {
                        config->nfapi_socket.p5_local_port = atoi(val);
                    } else if (strcmp(current_key, "p7_remote_port") == 0) {
                        config->nfapi_socket.p7_remote_port = atoi(val);
                    } else if (strcmp(current_key, "p7_local_port") == 0) {
                        config->nfapi_socket.p7_local_port = atoi(val);
                    } else if (strcmp(current_key, "enable_checksum") == 0) {
                        config->nfapi_socket.checksum_enabled = parse_yaml_bool(val);
                    } else if (strcmp(current_key, "ipv6_enabled") == 0) {
                        config->nfapi_socket.ipv6_enabled = parse_yaml_bool(val);
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_nfapi_socket_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

void parse_oai_forwarder_config(yaml_parser_t *parser, xFAPI_Config *config,
                                xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in oai_forwarder\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "recv_core_id") == 0) {
                        config->oai_forwarder.recv_core_id = atoi(val);
                    } else if (strcmp(current_key, "send_core_id") == 0) {
                        config->oai_forwarder.send_core_id = atoi(val);
                    } else if (strcmp(current_key, "priority") == 0) {
                        config->oai_forwarder.priority = atoi(val);
                    } else if (strcmp(current_key, "sched_policy") == 0) {
                        strncpy(config->oai_forwarder.sched_policy, val,
                                sizeof(config->oai_forwarder.sched_policy) - 1);
                        config->oai_forwarder.sched_policy[
                            sizeof(config->oai_forwarder.sched_policy) - 1] = '\0';
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_oai_forwarder_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}
#endif /* OAI_OCUDU */

#ifdef AERIAL_OCUDU

void parse_ocudu_xsm_l2_config(yaml_parser_t *parser, xFAPI_Config *config,
                                xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    parse_ocudu_xsm_endpoint_config(parser, &config->ocudu_xsm_l2);
}

void parse_nvipc_config(yaml_parser_t *parser, xFAPI_Config *config,
                        xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in nvipc\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "prefix") == 0) {
                        strncpy(config->nvipc.prefix, val,
                                sizeof(config->nvipc.prefix) - 1);
                        config->nvipc.prefix[sizeof(config->nvipc.prefix) - 1] = '\0';
                    } else if (strcmp(current_key, "blocking") == 0) {
                        config->nvipc.blocking = parse_yaml_bool(val);
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_nvipc_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

void parse_aerial_forwarder_config(yaml_parser_t *parser, xFAPI_Config *config,
                                   xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in aerial_forwarder\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "recv_core_id") == 0) {
                        config->aerial_forwarder.recv_core_id = atoi(val);
                    } else if (strcmp(current_key, "send_core_id") == 0) {
                        config->aerial_forwarder.send_core_id = atoi(val);
                    } else if (strcmp(current_key, "priority") == 0) {
                        config->aerial_forwarder.priority = atoi(val);
                    } else if (strcmp(current_key, "sched_policy") == 0) {
                        strncpy(config->aerial_forwarder.sched_policy, val,
                                sizeof(config->aerial_forwarder.sched_policy) - 1);
                        config->aerial_forwarder.sched_policy[
                            sizeof(config->aerial_forwarder.sched_policy) - 1] = '\0';
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_aerial_forwarder_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}
#endif /* AERIAL_OCUDU */

#ifdef AERIAL_OAI

void parse_aerial_oai_nvipc_config(yaml_parser_t *parser, xFAPI_Config *config,
                                   xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0, depth = 0, expecting_key = 1;
    char current_key[64] = "";

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in nvipc\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT: depth++; expecting_key = 1; break;
            case YAML_MAPPING_END_EVENT:   depth--; if (depth == 0) done = 1; break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "prefix") == 0) {
                        strncpy(config->nvipc.prefix, val, sizeof(config->nvipc.prefix) - 1);
                        config->nvipc.prefix[sizeof(config->nvipc.prefix) - 1] = '\0';
                    } else if (strcmp(current_key, "blocking") == 0) {
                        config->nvipc.blocking = parse_yaml_bool(val);
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_aerial_oai_nvipc_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default: break;
        }
        yaml_event_delete(&event);
    }
}

void parse_aerial_oai_nfapi_socket_config(yaml_parser_t *parser, xFAPI_Config *config,
                                          xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0, depth = 0, expecting_key = 1;
    char current_key[64] = "";

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in nfapi_socket\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT: depth++; expecting_key = 1; break;
            case YAML_MAPPING_END_EVENT:   depth--; if (depth == 0) done = 1; break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "remote_ip") == 0) {
                        strncpy(config->nfapi_socket.remote_ip, val,
                                sizeof(config->nfapi_socket.remote_ip) - 1);
                        config->nfapi_socket.remote_ip[sizeof(config->nfapi_socket.remote_ip) - 1] = '\0';
                    } else if (strcmp(current_key, "local_ip") == 0) {
                        strncpy(config->nfapi_socket.local_ip, val,
                                sizeof(config->nfapi_socket.local_ip) - 1);
                        config->nfapi_socket.local_ip[sizeof(config->nfapi_socket.local_ip) - 1] = '\0';
                    } else if (strcmp(current_key, "p5_remote_port") == 0) {
                        config->nfapi_socket.p5_remote_port = atoi(val);
                    } else if (strcmp(current_key, "p5_local_port") == 0) {
                        config->nfapi_socket.p5_local_port = atoi(val);
                    } else if (strcmp(current_key, "p7_local_port") == 0) {
                        config->nfapi_socket.p7_local_port = atoi(val);
                    } else if (strcmp(current_key, "p7_remote_port") == 0) {
                        config->nfapi_socket.p7_remote_port = atoi(val);
                    } else if (strcmp(current_key, "enable_checksum") == 0) {
                        config->nfapi_socket.checksum_enabled = parse_yaml_bool(val);
                    } else if (strcmp(current_key, "ipv6_enabled") == 0) {
                        config->nfapi_socket.ipv6_enabled = parse_yaml_bool(val);
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_aerial_oai_nfapi_socket_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default: break;
        }
        yaml_event_delete(&event);
    }
}

void parse_aerial_oai_forwarder_config(yaml_parser_t *parser, xFAPI_Config *config,
                                       xFAPI_ConfigFlags *config_flags) {
    (void)config_flags;
    yaml_event_t event;
    int done = 0, depth = 0, expecting_key = 1;
    char current_key[64] = "";

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in forwarder\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT: depth++; expecting_key = 1; break;
            case YAML_MAPPING_END_EVENT:   depth--; if (depth == 0) done = 1; break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "recv_core_id") == 0) {
                        config->forwarder.recv_core_id = atoi(val);
                    } else if (strcmp(current_key, "send_core_id") == 0) {
                        config->forwarder.send_core_id = atoi(val);
                    } else if (strcmp(current_key, "priority") == 0) {
                        config->forwarder.priority = atoi(val);
                    } else if (strcmp(current_key, "sched_policy") == 0) {
                        strncpy(config->forwarder.sched_policy, val,
                                sizeof(config->forwarder.sched_policy) - 1);
                        config->forwarder.sched_policy[sizeof(config->forwarder.sched_policy) - 1] = '\0';
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_,
                                "parse_aerial_oai_forwarder_config: Unknown key '%s'", current_key);
                    }
                    expecting_key = 1;
                }
                break;
            default: break;
        }
        yaml_event_delete(&event);
    }
}
#endif /* AERIAL_OAI */

void parse_simulation_mode_config(yaml_parser_t *parser, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_section_key[64] = "";
    char current_param_key[64] = "";
    int expecting_key = 1;
    thread_task_t *current_task_ptr = NULL;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in simulation_mode_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                } else if (depth == 1) {
                    current_section_key[0] = '\0';
                    current_task_ptr = NULL;
                    expecting_key = 1;
                }

                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_section_key, (char *)event.data.scalar.value, sizeof(current_section_key) - 1);
                    current_section_key[sizeof(current_section_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key && strcmp(current_section_key, "mode") == 0) {
                    config->simulation_mode.mode = atoi((char*)event.data.scalar.value);
                    config_flags->simulation_mode.mode = 1;
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

void parse_horizontal_levels(yaml_parser_t *parser, horizontal_log_t *levels, horizontal_log_flags_t *level_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 1;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in horizontal_levels\n", parser->error);
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {

                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {

                    const char *val_str = (char *)event.data.scalar.value;
                    int bool_val = parse_yaml_bool(val_str);
                    if (strcmp(current_key, "xFAPI_log") == 0){
                        levels->xFAPI_log = bool_val;
                        level_flags->xFAPI_log = 1;}
                    else if (strcmp(current_key, "xSM_log") == 0){
                        levels->xSM_log = bool_val;
                        level_flags->xSM_log = 1;}
                    else if (strcmp(current_key, "P5_log") == 0){
                        levels->P5_log = bool_val;
                        level_flags->P5_log = 1;}
                    else if (strcmp(current_key, "P7_log") == 0){
                        levels->P7_log = bool_val;
                        level_flags->P7_log = 1;}
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

void parse_terminal_logging_config(yaml_parser_t *parser, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in terminal_logging_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;

                if (depth == 2 && strcmp(current_key, "horizontal_level") == 0) {
                    parse_horizontal_levels(parser, &config->logging.horizontal_level, &config_flags->logging.horizontal_level);

                    depth--;
                    expecting_key = 1;
                }
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "print_config") == 0) {
                        config->logging.print_config = parse_yaml_bool(val);
                        config_flags->logging.print_config = 1;
                    } else if (strcmp(current_key, "print_datetime") == 0) {
                        config->logging.print_datetime = parse_yaml_bool(val);
                        config_flags->logging.print_datetime = 1;
                    } else if (strcmp(current_key, "vertical_level") == 0) {
                        strncpy(config->logging.vertical_level, val, sizeof(config->logging.vertical_level) - 1);
                        config->logging.vertical_level[sizeof(config->logging.vertical_level) - 1] = '\0';
                        config_flags->logging.vertical_level = 1;
                    }

                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

void parse_log_file_config(yaml_parser_t *parser, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in log_file_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                if (depth == 2 && strcmp(current_key, "horizontal_level") == 0) {
                    parse_horizontal_levels(parser, &config->log_file.horizontal_level,
                                            &config_flags->log_file.horizontal_level);
                    depth--;
                    expecting_key = 1;
                }
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "generate_log_file") == 0) {
                        config->log_file.generate_log_file = parse_yaml_bool(val);
                        config_flags->log_file.generate_log_file = 1;
                    } else if (strcmp(current_key, "file_size") == 0) {
                        config->log_file.file_size = atoi(val);
                        config_flags->log_file.file_size = 1;
                    } else if (strcmp(current_key, "print_config") == 0) {
                        config->log_file.print_config = parse_yaml_bool(val);
                        config_flags->log_file.print_config = 1;
                    } else if (strcmp(current_key, "print_datetime") == 0) {
                        config->log_file.print_datetime = parse_yaml_bool(val);
                        config_flags->log_file.print_datetime = 1;
                    } else if (strcmp(current_key, "vertical_level") == 0) {
                        strncpy(config->log_file.vertical_level, val, sizeof(config->log_file.vertical_level) - 1);
                        config->log_file.vertical_level[sizeof(config->log_file.vertical_level) - 1] = '\0';
                        config_flags->log_file.vertical_level = 1;
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

void parse_dashboard_config(yaml_parser_t *parser, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in dashboard_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "enabled") == 0) {
                        config->dashboard.enabled = parse_yaml_bool(val);
                        config_flags->dashboard.enabled = 1;
                    } else if (strcmp(current_key, "core_id") == 0) {
                        config->dashboard.core_id = atoi(val);
                        config_flags->dashboard.core_id = 1;
                    } else if (strcmp(current_key, "bind_ip") == 0) {
                        strncpy(config->dashboard.bind_ip, val, sizeof(config->dashboard.bind_ip) - 1);
                        config->dashboard.bind_ip[sizeof(config->dashboard.bind_ip) - 1] = '\0';
                        config_flags->dashboard.bind_ip = 1;
                    } else if (strcmp(current_key, "port") == 0) {
                        config->dashboard.port = atoi(val);
                        config_flags->dashboard.port = 1;
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

void parse_stats_file_generation_config(yaml_parser_t *parser, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    char current_key[64] = "";
    int expecting_key = 1;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Parser error %d in stats_file_generation_config\n", parser->error);
            break;
        }

        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_key, (char *)event.data.scalar.value, sizeof(current_key) - 1);
                    current_key[sizeof(current_key) - 1] = '\0';
                    expecting_key = 0;
                } else if (depth == 1 && !expecting_key) {
                    const char *val = (char *)event.data.scalar.value;
                    if (strcmp(current_key, "generate_summary_file") == 0) {
                        config->stats_file_generation.generate_summary_file = parse_yaml_bool(val);
                        config_flags->stats_file_generation.generate_summary_file = 1;
                    } else if (strcmp(current_key, "generate_detailed_file") == 0) {
                        config->stats_file_generation.generate_detailed_file = parse_yaml_bool(val);
                        config_flags->stats_file_generation.generate_detailed_file = 1;
                    } else if (strcmp(current_key, "capture_ul_msgs") == 0) {
                        config->stats_file_generation.capture_ul_msgs = parse_yaml_bool(val);
                        config_flags->stats_file_generation.capture_ul_msgs = 1;
                    } else if (strcmp(current_key, "capture_dl_msgs") == 0) {
                        config->stats_file_generation.capture_dl_msgs = parse_yaml_bool(val);
                        config_flags->stats_file_generation.capture_dl_msgs = 1;
                    } else if (strcmp(current_key, "capture_p5_msgs") == 0) {
                        config->stats_file_generation.capture_p5_msgs = parse_yaml_bool(val);
                        config_flags->stats_file_generation.capture_p5_msgs = 1;
                    } else if (strcmp(current_key, "capture_p7_msgs") == 0) {
                        config->stats_file_generation.capture_p7_msgs = parse_yaml_bool(val);
                        config_flags->stats_file_generation.capture_p7_msgs = 1;
                    }
                    expecting_key = 1;
                }
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
}

int parse_yaml_main(const char *filename, AppContext *app_ctx) {
    xFAPI_Config *config = &app_ctx->config;
    xFAPI_ConfigFlags *config_flags = &app_ctx->config_flags;
    FILE *fh = fopen(filename, "r");
    if (!fh) {
        SM_Logs(LOG_ERROR, _XFAPI_, "Failed to open YAML config '%s': %s",
                filename, strerror(errno));
        return -1;
    }
    SM_Logs(LOG_INFO, _XFAPI_, "Parsing YAML config file: %s", filename);

    yaml_parser_t parser;
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    int expecting_key = 0;
    int parse_ok = 1;
    char current_top_key[64] = "";

    if (!yaml_parser_initialize(&parser)) {
        SM_Logs(LOG_ERROR, _XFAPI_, "Failed to initialize YAML parser for '%s'.", filename);
        fclose(fh);
        return -1;
    }
    yaml_parser_set_input_file(&parser, fh);

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            SM_Logs(LOG_ERROR, _XFAPI_,
                    "YAML parse error (code %d) at offset %zu in '%s' — config is incomplete.",
                    parser.error, (size_t)parser.offset, filename);
            parse_ok = 0;
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:

                depth++;
                expecting_key = 1;

                break;
            case YAML_MAPPING_END_EVENT:

                depth--;
                if (depth == 0) {
                    done = 1;
                } else if (depth == 1) {

                    expecting_key = 1;
                    current_top_key[0] = '\0';
                }

                break;
            case YAML_SCALAR_EVENT:

                if (depth == 1 && expecting_key) {

                    strncpy(current_top_key, (char *)event.data.scalar.value, sizeof(current_top_key)-1);
                    current_top_key[sizeof(current_top_key)-1] = '\0';
                    expecting_key = 0;

                    if (strcmp(current_top_key, "core_config") == 0) {
                        parse_core_config(&parser, &config->core_config, &config_flags->core_config);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "dpdk_config") == 0) {
                        parse_dpdk_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "wls_config") == 0) {
                        parse_wls_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "simulation_mode") == 0) {
                        parse_simulation_mode_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "logging") == 0) {
                        parse_terminal_logging_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "log_file") == 0) {
                        parse_log_file_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "dashboard") == 0) {
                        parse_dashboard_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "stats_file_generation") == 0) {
                        parse_stats_file_generation_config(&parser, config, config_flags);
                        expecting_key = 1;
                    }
#ifdef OCUDU_OCUDU
                    else if (strcmp(current_top_key, "ocudu_xsm_l1") == 0) {
                        parse_ocudu_xsm_l1_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "ocudu_xsm_l2") == 0) {
                        parse_ocudu_xsm_l2_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "ocudu_forwarder") == 0) {
                        parse_ocudu_forwarder_config(&parser, config, config_flags);
                        expecting_key = 1;
                    }
#endif
#ifdef OAI_OCUDU
                    else if (strcmp(current_top_key, "ocudu_xsm_l2") == 0) {
                        parse_ocudu_xsm_l2_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "nfapi_socket") == 0) {
                        parse_nfapi_socket_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "oai_forwarder") == 0) {
                        parse_oai_forwarder_config(&parser, config, config_flags);
                        expecting_key = 1;
                    }
#endif
#ifdef AERIAL_OCUDU
                    else if (strcmp(current_top_key, "ocudu_xsm_l2") == 0) {
                        parse_ocudu_xsm_l2_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "nvipc") == 0) {
                        parse_nvipc_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "aerial_forwarder") == 0) {
                        parse_aerial_forwarder_config(&parser, config, config_flags);
                        expecting_key = 1;
                    }
#endif
#ifdef AERIAL_OAI
                    else if (strcmp(current_top_key, "nvipc") == 0) {
                        parse_aerial_oai_nvipc_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "nfapi_socket") == 0) {
                        parse_aerial_oai_nfapi_socket_config(&parser, config, config_flags);
                        expecting_key = 1;
                    } else if (strcmp(current_top_key, "forwarder") == 0) {
                        parse_aerial_oai_forwarder_config(&parser, config, config_flags);
                        expecting_key = 1;
                    }
#endif
                    else {

                        printf("Skipping unknown top-level key: %s\n", current_top_key);

                    }
                } else if (depth == 1 && !expecting_key) {

                    expecting_key = 1;
                }
                break;
            case YAML_DOCUMENT_END_EVENT:

                break;
            case YAML_STREAM_END_EVENT:

                done = 1;
                break;
            default:

                break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(fh);
    return parse_ok ? 0 : -1;
}

int parse_yaml_sim_mode(const char *filename, xFAPI_Config *config, xFAPI_ConfigFlags *config_flags) {
    FILE *fh = fopen(filename, "r");
    if (!fh) {
        SM_Logs(LOG_ERROR, _XFAPI_, "Failed to open sim YAML '%s': %s",
                filename, strerror(errno));
        return -1;
    }

    yaml_parser_t parser;
    yaml_event_t event;
    int done = 0;
    int depth = 0;
    int expecting_key = 0;
    int parse_ok = 1;
    char current_top_key[64] = "";

    if (!yaml_parser_initialize(&parser)) {
        SM_Logs(LOG_ERROR, _XFAPI_, "Failed to initialize YAML parser for '%s'.", filename);
        fclose(fh);
        return -1;
    }
    yaml_parser_set_input_file(&parser, fh);

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            SM_Logs(LOG_ERROR, _XFAPI_,
                    "YAML parse error (code %d) at offset %zu in '%s' — sim config is incomplete.",
                    parser.error, (size_t)parser.offset, filename);
            parse_ok = 0;
            break;
        }
        switch (event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                expecting_key = 1;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                if (depth == 0) {
                    done = 1;
                } else if (depth == 1) {
                    expecting_key = 1;
                    current_top_key[0] = '\0';
                }
                break;
            case YAML_SCALAR_EVENT:
                if (depth == 1 && expecting_key) {
                    strncpy(current_top_key, (char *)event.data.scalar.value, sizeof(current_top_key)-1);
                    current_top_key[sizeof(current_top_key)-1] = '\0';
                    expecting_key = 0;

                    if (strcmp(current_top_key, "core_config") == 0) {
                        parse_sim_mode_core_config(&parser,
                            &config->simulation_mode.core_config,
                            &config_flags->simulation_mode.core_config);
                        expecting_key = 1;
                    } else {
                        SM_Logs(LOG_WARN, _XFAPI_, "Skipping unknown top-level key: %s", current_top_key);
                    }
                } else if (depth == 1 && !expecting_key) {
                    expecting_key = 1;
                }
                break;
            case YAML_DOCUMENT_END_EVENT:
                break;
            case YAML_STREAM_END_EVENT:
                done = 1;
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(fh);
    return parse_ok ? 0 : -1;
}

void print_config_flags(const xFAPI_ConfigFlags *flags) {
    printf("======== CONFIGURATION FILL STATUS ========\n");

    printf("\n--- Core Config ---\n");
    printf("rx_task: core_id=%d, priority=%d, sched_policy=%d\n",
           flags->core_config.rx_task.core_id,
           flags->core_config.rx_task.priority,
           flags->core_config.rx_task.sched_policy);
    printf("tx_task: core_id=%d, priority=%d, sched_policy=%d\n",
           flags->core_config.tx_task.core_id,
           flags->core_config.tx_task.priority,
           flags->core_config.tx_task.sched_policy);
    printf("xSM_recv_task: core_id=%d, priority=%d, sched_policy=%d\n",
           flags->core_config.xSM_recv_task.core_id,
           flags->core_config.xSM_recv_task.priority,
           flags->core_config.xSM_recv_task.sched_policy);
    printf("dashboard_task: core_id=%d, priority=%d, sched_policy=%d\n",
           flags->core_config.dashboard_task.core_id,
           flags->core_config.dashboard_task.priority,
           flags->core_config.dashboard_task.sched_policy);

    printf("\n--- DPDK Config ---\n");
    printf("dpdk_iova_mode=%d, dpdk_memory_zone=%d\n",
           flags->dpdk_config.dpdk_iova_mode,
           flags->dpdk_config.dpdk_memory_zone);

    printf("\n--- WLS Config ---\n");
    printf("wls_device_name=%d, wls_mem_size=%d\n",
           flags->wls_config.wls_device_name,
           flags->wls_config.wls_mem_size);

    printf("\n--- Simulation Mode ---\n");
    printf("mode=%d\n", flags->simulation_mode.mode);

    printf("\n--- Logging Config ---\n");
    printf("vertical_level=%d, print_config=%d, print_datetime=%d\n",
           flags->logging.vertical_level,
           flags->logging.print_config,
           flags->logging.print_datetime);
    printf("Horizontal log: xFAPI=%d, xSM=%d, P5=%d, P7=%d\n",
           flags->logging.horizontal_level.xFAPI_log,
           flags->logging.horizontal_level.xSM_log,
           flags->logging.horizontal_level.P5_log,
           flags->logging.horizontal_level.P7_log);

    printf("\n--- Log File Config ---\n");
    printf("generate_log_file=%d, file_size=%d, vertical_level=%d, print_datetime=%d, print_config=%d\n",
           flags->log_file.generate_log_file,
           flags->log_file.file_size,
           flags->log_file.vertical_level,
           flags->log_file.print_datetime,
           flags->log_file.print_config);
    printf("Horizontal log: xFAPI=%d, xSM=%d, P5=%d, P7=%d\n",
           flags->log_file.horizontal_level.xFAPI_log,
           flags->log_file.horizontal_level.xSM_log,
           flags->log_file.horizontal_level.P5_log,
           flags->log_file.horizontal_level.P7_log);

    printf("\n--- Dashboard Config ---\n");
    printf("enabled=%d, bind_ip=%d, port=%d, core_id=%d\n",
           flags->dashboard.enabled,
           flags->dashboard.bind_ip,
           flags->dashboard.port,
           flags->dashboard.core_id);

    printf("\n--- Stats File Generation ---\n");
    printf("summary=%d, detailed=%d, UL=%d, DL=%d, P5=%d, P7=%d\n",
           flags->stats_file_generation.generate_summary_file,
           flags->stats_file_generation.generate_detailed_file,
           flags->stats_file_generation.capture_ul_msgs,
           flags->stats_file_generation.capture_dl_msgs,
           flags->stats_file_generation.capture_p5_msgs,
           flags->stats_file_generation.capture_p7_msgs);

    printf("============================================\n");
}
