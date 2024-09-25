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

#ifndef YAML_UTILS_H
#define YAML_UTILS_H

#include <stdlib.h>
#include <string.h>

static inline int parse_yaml_bool(const char *val) {
    if (val == NULL) return 0;
    if (strcmp(val, "true") == 0 || strcmp(val, "True") == 0 || strcmp(val, "TRUE") == 0) {
        return 1;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "False") == 0 || strcmp(val, "FALSE") == 0) {
        return 0;
    }
    return atoi(val);
}

#endif
