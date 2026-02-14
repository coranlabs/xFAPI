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

#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <stdint.h>
#include <stddef.h>

typedef struct buffer_pool buffer_pool_t;

buffer_pool_t *buffer_pool_create(size_t slot_size, int num_slots);
int            buffer_pool_acquire(buffer_pool_t *pool, uint64_t *pa_out);
void           buffer_pool_release(buffer_pool_t *pool, uint64_t pa);
void           buffer_pool_destroy(buffer_pool_t *pool);

#endif /* BUFFER_POOL_H */
