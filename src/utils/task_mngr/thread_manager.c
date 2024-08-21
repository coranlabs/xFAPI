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

#include "thread_manager.h"
#include <sched.h>
#include <stdlib.h>
#include <string.h>

void *thread_entrypoint(void *arg)
{
    (void)arg;
    return NULL;
}

int thread_wait(thread_mgt_t *task)
{
    return pthread_join(task->thread, NULL);
}

int thread_stop(thread_mgt_t *task)
{
    task->running = 0;
    return 0;
}

uint8_t create_managed_thread(
    thread_mgt_t *task,
    void *(*start_routine)(void *),
    void *arg,
    const char *name,
    int core_id,
    int priority)
{
    (void)name;
    if (pthread_create(&task->thread, NULL, start_routine, arg) != 0)
        return 0;
    if (core_id >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core_id, &set);
        pthread_setaffinity_np(task->thread, sizeof(set), &set);
    }
    if (priority > 0) {
        struct sched_param sp = { .sched_priority = priority };
        pthread_setschedparam(task->thread, SCHED_FIFO, &sp);
    }
    return 1;
}
