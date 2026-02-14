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

#include "buffer_pool.h"
#include "unified_logger.h"
#include <stdlib.h>
#include <string.h>

struct buffer_pool {
    size_t    slot_size;
    int       num_slots;
    int       head;
    uint64_t *slots;
};

buffer_pool_t *buffer_pool_create(size_t slot_size, int num_slots)
{
    buffer_pool_t *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->slot_size = slot_size;
    p->num_slots = num_slots;
    p->slots     = calloc(num_slots, sizeof(uint64_t));
    SM_Logs(LOG_INFO, _XFAPI_, "[BUFFER_POOL] Created pool: %d slots x %zu bytes",
            num_slots, slot_size);
    return p;
}

int buffer_pool_acquire(buffer_pool_t *pool, uint64_t *pa_out)
{
    if (pool->head >= pool->num_slots) return -1;
    *pa_out = pool->slots[pool->head++];
    return 0;
}

void buffer_pool_release(buffer_pool_t *pool, uint64_t pa)
{
    if (pool->head > 0)
        pool->slots[--pool->head] = pa;
}

void buffer_pool_destroy(buffer_pool_t *pool)
{
    if (!pool) return;
    free(pool->slots);
    free(pool);
}
