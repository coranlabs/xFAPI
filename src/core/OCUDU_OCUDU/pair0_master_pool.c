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


#include "pair0_master_pool.h"

#ifdef OCUDU_OCUDU

#include "xsm/xsm.h"
#include "unified_logger.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define POOL_BLOCK_SIZE    (128u * 1024u)
#define POOL_BLOCK_COUNT   2048u
#define DL_SLOT_DEPTH      16u
#define DL_MAX_PER_SLOT    256u

typedef struct {
    uint32_t buffers[DL_MAX_PER_SLOT];
    uint32_t count;
} slot_t;

struct pair0_master_pool {
    uint8_t* base_va;
    uint32_t block_size;
    uint32_t block_count;

    uint32_t* free_list;
    uint32_t  free_top;

    slot_t slots[DL_SLOT_DEPTH];

    pthread_mutex_t mutex;

    uint64_t total_acquired;
    uint64_t total_recycled;
    uint64_t acquire_failures;
};

pair0_master_pool_t* pair0_master_pool_create(void* region_va, uint64_t region_size)
{
    if (region_va == NULL) {
        return NULL;
    }
    const uint64_t needed = (uint64_t)POOL_BLOCK_SIZE * POOL_BLOCK_COUNT;
    if (region_size < needed) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OCUDU_BRIDGE] master pool: region too small (%llu < %llu)",
                (unsigned long long)region_size, (unsigned long long)needed);
        return NULL;
    }

    pair0_master_pool_t* pool = (pair0_master_pool_t*)calloc(1, sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }
    pool->base_va     = (uint8_t*)region_va;
    pool->block_size  = POOL_BLOCK_SIZE;
    pool->block_count = POOL_BLOCK_COUNT;

    pool->free_list = (uint32_t*)calloc(POOL_BLOCK_COUNT, sizeof(uint32_t));
    if (pool->free_list == NULL) {
        free(pool);
        return NULL;
    }

    for (uint32_t i = 0; i < POOL_BLOCK_COUNT; ++i) {
        pool->free_list[i] = POOL_BLOCK_COUNT - 1u - i;
    }
    pool->free_top = POOL_BLOCK_COUNT;

    pthread_mutex_init(&pool->mutex, NULL);
    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_BRIDGE] Master pool ready: base=%p blocks=%u x %u bytes (%llu MB), "
            "slot-depth=%u, max-per-slot=%u",
            pool->base_va, POOL_BLOCK_COUNT, POOL_BLOCK_SIZE,
            (unsigned long long)((uint64_t)POOL_BLOCK_COUNT * POOL_BLOCK_SIZE >> 20),
            DL_SLOT_DEPTH, DL_MAX_PER_SLOT);
    return pool;
}

void pair0_master_pool_destroy(pair0_master_pool_t* pool)
{
    if (pool == NULL) {
        return;
    }
    SM_Logs(LOG_INFO, _XFAPI_,
            "[OCUDU_BRIDGE] Master pool stats: acquired=%llu recycled=%llu failures=%llu",
            (unsigned long long)pool->total_acquired,
            (unsigned long long)pool->total_recycled,
            (unsigned long long)pool->acquire_failures);
    pthread_mutex_destroy(&pool->mutex);
    free(pool->free_list);
    free(pool);
}

void* pair0_master_pool_acquire(pair0_master_pool_t* pool)
{
    if (pool == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&pool->mutex);

    if (pool->free_top == 0u) {

        ++pool->acquire_failures;
        if ((pool->acquire_failures & 0x3FFu) == 1u) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OCUDU_BRIDGE] master pool exhausted (count=%llu); is L1 emitting SLOT.indications?",
                    (unsigned long long)pool->acquire_failures);
        }
        pthread_mutex_unlock(&pool->mutex);
        return NULL;
    }

    uint32_t idx = pool->free_list[--pool->free_top];

    slot_t* cur = &pool->slots[DL_SLOT_DEPTH - 1u];
    if (cur->count >= DL_MAX_PER_SLOT) {

        pool->free_list[pool->free_top++] = idx;
        ++pool->acquire_failures;
        pthread_mutex_unlock(&pool->mutex);
        return NULL;
    }
    cur->buffers[cur->count++] = idx;
    ++pool->total_acquired;

    void* va = pool->base_va + (uint64_t)idx * pool->block_size;
    pthread_mutex_unlock(&pool->mutex);
    return va;
}

uint64_t pair0_master_pool_va_to_pa(pair0_master_pool_t* pool,
                                    xsm_handle_t*        handle,
                                    void*                va)
{
    (void)pool;
    return XSM_VirtToPhys(handle, va);
}

void pair0_master_pool_on_slot_indication(pair0_master_pool_t* pool)
{
    if (pool == NULL) {
        return;
    }
    pthread_mutex_lock(&pool->mutex);

    slot_t* oldest = &pool->slots[0];
    for (uint32_t i = 0; i < oldest->count; ++i) {
        pool->free_list[pool->free_top++] = oldest->buffers[i];
    }
    pool->total_recycled += oldest->count;
    oldest->count = 0u;

    for (uint32_t k = 0; k + 1u < DL_SLOT_DEPTH; ++k) {
        pool->slots[k] = pool->slots[k + 1u];
    }
    pool->slots[DL_SLOT_DEPTH - 1u].count = 0u;

    pthread_mutex_unlock(&pool->mutex);
}

#endif
