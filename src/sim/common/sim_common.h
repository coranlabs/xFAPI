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

#ifndef SIM_COMMON_H
#define SIM_COMMON_H

#include "../../utils/config/yaml_config.h"
#include "../../include/common_global.h"

typedef enum {
    SIM_MODE_DISABLED = 0,
} sim_mode_t;

sim_mode_t get_current_sim_mode(const xFAPI_Config* config);
#endif
