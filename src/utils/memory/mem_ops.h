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

#ifndef MEM_OPS_H_
#define MEM_OPS_H_

#define NR5G_FAPI_MEMCPY(d, x, s, n) nr5g_fapi_memcpy_bound_check(d, x, s, n)
#define NR5G_FAPI_MEMSET(s, x, c, n) nr5g_fapi_memset_bound_check(s, x, c, n)
#define NR5G_FAPI_STRCPY(d, x, s, n) nr5g_fapi_strcpy_bound_check(d, x, s, n)

#define MSG_MAXSIZE  (16 * 16384)

#include <stdint.h>
#include <stddef.h>

uint8_t checked_memset(
    void * const s,
    size_t x,
    const int32_t c,
    size_t n);
uint8_t checked_strncpy(
    char * const d,
    size_t x,
    const char * const s,
    size_t n);

#endif
