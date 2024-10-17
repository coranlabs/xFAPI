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

#include "message_stats.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

message_stats_t g_message_stats[MAX_MESSAGE_STATS];
volatile atomic_uint_fast32_t g_stats_index = 0;

void init_message_stats(void) {
    memset(g_message_stats, 0, sizeof(g_message_stats));
    atomic_store(&g_stats_index, 0);
    printf("[MESSAGE_STATS] Initialized message stats array with %d elements\n", MAX_MESSAGE_STATS);
}

uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void add_message_stats(const char* msg_type, int sfn, int slot, int pdu_size,
                       int num_pdus, const char* content, uint64_t ipc_latency_ns) {
    if (!msg_type || !content) {
        return;
    }

    uint32_t current_index = atomic_fetch_add(&g_stats_index, 1) % MAX_MESSAGE_STATS;
    message_stats_t* stats = &g_message_stats[current_index];

    stats->timestamp_ns = get_timestamp_ns();
    strncpy(stats->message_type, msg_type, MAX_MESSAGE_TYPE_LEN - 1);
    stats->message_type[MAX_MESSAGE_TYPE_LEN - 1] = '\0';
    stats->sfn = sfn;
    stats->slot = slot;
    stats->pdu_size = pdu_size;
    stats->num_pdus = num_pdus;
    stats->ipc_latency_ns = ipc_latency_ns;
    strncpy(stats->message_content, content, MAX_MESSAGE_CONTENT_LEN - 1);
    stats->message_content[MAX_MESSAGE_CONTENT_LEN - 1] = '\0';
}

static void escape_json_string(FILE* fp, const char* str) {
    const char* p = str;
    while (*p) {
        switch (*p) {
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '"':  fputs("\\\"", fp); break;
            case '\b': fputs("\\b", fp); break;
            case '\f': fputs("\\f", fp); break;
            default:
                if ((unsigned char)*p < 32) {
                    fprintf(fp, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, fp);
                }
                break;
        }
        p++;
    }
}

void dump_message_stats_to_json(void) {
    FILE* fp = fopen("generated_logs/message_stats.json", "w");
    if (!fp) {
        printf("[MESSAGE_STATS] Error: Could not open generated_logs/message_stats.json for writing\n");
        return;
    }

    uint32_t total_messages = atomic_load(&g_stats_index);
    uint32_t messages_to_dump = (total_messages > MAX_MESSAGE_STATS) ? MAX_MESSAGE_STATS : total_messages;
    uint32_t start_index = (total_messages > MAX_MESSAGE_STATS) ? (total_messages % MAX_MESSAGE_STATS) : 0;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"total_messages_captured\": %u,\n", total_messages);
    fprintf(fp, "  \"messages_in_dump\": %u,\n", messages_to_dump);
    fprintf(fp, "  \"messages\": [\n");

    for (uint32_t i = 0; i < messages_to_dump; i++) {
        uint32_t index = (start_index + i) % MAX_MESSAGE_STATS;
        message_stats_t* stats = &g_message_stats[index];

        if (stats->timestamp_ns == 0) {
            continue;
        }

        fprintf(fp, "    {\n");
        fprintf(fp, "      \"timestamp_ns\": %lu,\n", stats->timestamp_ns);
        fprintf(fp, "      \"message_type\": \"%s\",\n", stats->message_type);
        fprintf(fp, "      \"sfn\": %d,\n", stats->sfn);
        fprintf(fp, "      \"slot\": %d,\n", stats->slot);
        fprintf(fp, "      \"pdu_size\": %d,\n", stats->pdu_size);
        fprintf(fp, "      \"num_pdus\": %d,\n", stats->num_pdus);
        fprintf(fp, "      \"ipc_latency_ns\": %lu,\n", stats->ipc_latency_ns);
        fprintf(fp, "      \"message_content\": \"");
        escape_json_string(fp, stats->message_content);
        fprintf(fp, "\"\n");
        fprintf(fp, "    }");

        if (i < messages_to_dump - 1) {
            fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    printf("[MESSAGE_STATS] Dumped %u messages to generated_logs/message_stats.json\n", messages_to_dump);
}
