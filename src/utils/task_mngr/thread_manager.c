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

#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>

#include "thread_manager.h"
#include "../../main/app_context.h"

static int policy_from_name(const char *name) {
    if (!name || !*name) return SCHED_OTHER;
    if (strcasecmp(name, "SCHED_FIFO")  == 0) return SCHED_FIFO;
    if (strcasecmp(name, "SCHED_RR")    == 0) return SCHED_RR;
    if (strcasecmp(name, "SCHED_OTHER") == 0) return SCHED_OTHER;
    if (strcasecmp(name, "SCHED_BATCH") == 0) return SCHED_BATCH;
    if (strcasecmp(name, "SCHED_IDLE")  == 0) return SCHED_IDLE;
    return SCHED_OTHER;
}

void *thread_entrypoint(void *arg) {
    thread_mgt_t *task = (thread_mgt_t *)arg;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(task->core_id, &cpuset);

    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "[ThreadManager] Failed to set affinity for %s (core %d): %s",
                task->name, task->core_id, strerror(ret));
    } else {
        SM_Logs(LOG_INFO, _XFAPI_,
                "[ThreadManager] Thread %s (ID %lu) pinned to core %d",
                task->name, (unsigned long)task->thread_id, task->core_id);
    }
    return task->start_routine(task->arg);
}

int thread_wait(thread_mgt_t *task) {
    if (!task) {
        SM_Logs(LOG_ERROR, _XFAPI_, "[ThreadManager] thread_wait: task is NULL.");
        return EINVAL;
    }
    int ret = pthread_join(task->thread_id, NULL);
    if (ret != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_, "[ThreadManager] Failed to join thread '%s': %s",
                task->name, strerror(ret));
    }
    return ret;
}

int thread_stop(thread_mgt_t *task) {
    if (!task) {
        SM_Logs(LOG_ERROR, _XFAPI_, "[ThreadManager] thread_stop: task is NULL.");
        return EINVAL;
    }
    task->should_run = 0;

    int ret = pthread_join(task->thread_id, NULL);
    if (ret != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "[ThreadManager] Failed to stop and join thread '%s': %s",
                task->name, strerror(ret));
    }
    return ret;
}

uint8_t create_managed_thread(
    thread_mgt_t *task,
    void *(*start_routine)(void *),
    void *arg,
    const char *name,
    int core_id,
    int priority,
    const char *sched_policy)
{
    if (!task || !start_routine || !name) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "[ThreadManager] Invalid arguments to create_managed_thread.");
        return FAILURE;
    }

    const int policy = policy_from_name(sched_policy);

    SM_Logs(LOG_INFO, _XFAPI_,
            "[ThreadManager] Creating thread '%s' (core=%d priority=%d policy=%s)",
            name, core_id, priority,
            sched_policy && *sched_policy ? sched_policy : "SCHED_OTHER");

    memset(task, 0, sizeof(thread_mgt_t));
    task->start_routine = start_routine;
    task->arg           = arg;
    task->core_id       = core_id;
    task->priority      = priority;
    task->should_run    = 1;

    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';

    itc_queue_init(&task->queue);

    pthread_attr_init(&task->thread_attr);

    int ret;
    if ((ret = pthread_attr_setschedpolicy(&task->thread_attr, policy)) != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "[ThreadManager] Failed to set scheduling policy for thread '%s': %s",
                task->name, strerror(ret));
        pthread_attr_destroy(&task->thread_attr);
        return FAILURE;
    }

    struct sched_param param;
    param.sched_priority = priority;
    if ((ret = pthread_attr_setschedparam(&task->thread_attr, &param)) != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "[ThreadManager] Failed to set scheduling priority for thread '%s': %s",
                task->name, strerror(ret));
        pthread_attr_destroy(&task->thread_attr);
        return FAILURE;
    }

    if ((ret = pthread_attr_setinheritsched(&task->thread_attr, PTHREAD_EXPLICIT_SCHED)) != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "[ThreadManager] Failed to set inheritsched for thread '%s': %s",
                task->name, strerror(ret));
        pthread_attr_destroy(&task->thread_attr);
        return FAILURE;
    }

    if ((ret = pthread_create(&task->thread_id, &task->thread_attr,
                              thread_entrypoint, task)) != 0) {
        SM_Logs(LOG_ERROR, _XFAPI_,
                "[ThreadManager] Failed to create thread '%s': %s",
                task->name, strerror(ret));
        pthread_attr_destroy(&task->thread_attr);
        return FAILURE;
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[ThreadManager] Thread '%s' created (ID %lu).",
            task->name, (unsigned long)task->thread_id);

    pthread_attr_destroy(&task->thread_attr);
    return SUCCESS;
}
