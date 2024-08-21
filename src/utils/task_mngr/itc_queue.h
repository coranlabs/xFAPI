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

#ifndef ITC_QUEUE_H
#define ITC_QUEUE_H

#include <pthread.h>
#include <stdbool.h>

#define ITC_QUEUE_CAPACITY 50

typedef struct {
    void* buffer[ITC_QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    bool is_active;

    pthread_mutex_t mutex;
    pthread_cond_t can_produce;
    pthread_cond_t can_consume;

} ITC_Queue_t;

void itc_queue_init(ITC_Queue_t* q);

void itc_queue_push(ITC_Queue_t* q, void* item);

void* itc_queue_pop(ITC_Queue_t* q);

void itc_queue_destroy(ITC_Queue_t* q);

#endif
