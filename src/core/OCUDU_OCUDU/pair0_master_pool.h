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


#ifndef PAIR0_MASTER_POOL_H
#define PAIR0_MASTER_POOL_H

#ifdef OCUDU_OCUDU

#include <stddef.h>
#include <stdint.h>

#include "xsm/xsm.h"

typedef struct pair0_master_pool pair0_master_pool_t;

pair0_master_pool_t* pair0_master_pool_create(void* region_va, uint64_t region_size);

void pair0_master_pool_destroy(pair0_master_pool_t* pool);

void* pair0_master_pool_acquire(pair0_master_pool_t* pool);

uint64_t pair0_master_pool_va_to_pa(pair0_master_pool_t* pool,
                                    xsm_handle_t*        handle,
                                    void*                va);

void pair0_master_pool_on_slot_indication(pair0_master_pool_t* pool);

#endif

#endif
