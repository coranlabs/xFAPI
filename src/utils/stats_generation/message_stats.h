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

#ifndef MESSAGE_STATS_H
#define MESSAGE_STATS_H

#include <stdint.h>
#include <time.h>
#include <stdatomic.h>

#define MAX_MESSAGE_STATS 100000
#define MAX_MESSAGE_TYPE_LEN 64
#define MAX_MESSAGE_CONTENT_LEN 8192

typedef struct {
    uint64_t timestamp_ns;
    char message_type[MAX_MESSAGE_TYPE_LEN];
    int sfn;
    int slot;
    int pdu_size;
    int num_pdus;
    uint64_t ipc_latency_ns;
    char message_content[MAX_MESSAGE_CONTENT_LEN];
} message_stats_t;

extern message_stats_t g_message_stats[MAX_MESSAGE_STATS];
extern volatile atomic_uint_fast32_t g_stats_index;

void init_message_stats(void);
void add_message_stats(const char* msg_type, int sfn, int slot, int pdu_size,
                       int num_pdus, const char* content, uint64_t ipc_latency_ns);
void dump_message_stats_to_json(void);
uint64_t get_timestamp_ns(void);

#endif
