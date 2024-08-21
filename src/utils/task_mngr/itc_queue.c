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

#include "itc_queue.h"
#include <stdio.h>

void itc_queue_init(ITC_Queue_t* q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->is_active = true;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->can_produce, NULL);
    pthread_cond_init(&q->can_consume, NULL);
}

void itc_queue_push(ITC_Queue_t* q, void* item) {
    pthread_mutex_lock(&q->mutex);

    while (q->count == ITC_QUEUE_CAPACITY && q->is_active) {
        pthread_cond_wait(&q->can_produce, &q->mutex);
    }

    if (!q->is_active) {
        pthread_mutex_unlock(&q->mutex);
        return;
    }

    q->buffer[q->tail] = item;
    q->tail = (q->tail + 1) % ITC_QUEUE_CAPACITY;
    q->count++;

    pthread_cond_signal(&q->can_consume);

    pthread_mutex_unlock(&q->mutex);
}

void* itc_queue_pop(ITC_Queue_t* q) {
    pthread_mutex_lock(&q->mutex);

    while (q->count == 0 && q->is_active) {
        pthread_cond_wait(&q->can_consume, &q->mutex);
    }

    if (!q->is_active && q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }

    void* item = q->buffer[q->head];
    q->head = (q->head + 1) % ITC_QUEUE_CAPACITY;
    q->count--;

    pthread_cond_signal(&q->can_produce);

    pthread_mutex_unlock(&q->mutex);
    return item;
}

void itc_queue_destroy(ITC_Queue_t* q) {
    pthread_mutex_lock(&q->mutex);
    q->is_active = false;

    pthread_cond_broadcast(&q->can_produce);
    pthread_cond_broadcast(&q->can_consume);
    pthread_mutex_unlock(&q->mutex);

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->can_produce);
    pthread_cond_destroy(&q->can_consume);
}
