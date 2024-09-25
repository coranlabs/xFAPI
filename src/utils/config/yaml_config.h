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


#ifndef YAML_CONFIG_H
#define YAML_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common_global.h"
#include "../../main/app_context.h"

int  parse_yaml_main(const char *filename, AppContext *app_context);
int  parse_yaml_sim_mode(const char *filename, xFAPI_Config *config,
                         xFAPI_ConfigFlags *config_flags);
void print_config_flags(const xFAPI_ConfigFlags *flags);

#ifdef __cplusplus
}
#endif

#endif
