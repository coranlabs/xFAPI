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


#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H
#include "unified_logger.h"
#include <pthread.h>

void *thread_entrypoint(void *arg);

int thread_wait(thread_mgt_t *task);

int thread_stop(thread_mgt_t *task);

uint8_t create_managed_thread(
    thread_mgt_t *task,
    void *(*start_routine)(void *),
    void *arg,
    const char *name,
    int core_id,
    int priority,
    const char *sched_policy);

#endif
