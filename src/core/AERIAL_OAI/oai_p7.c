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

// AERIAL_OAI PNF — P7 (UDP) data plane. RX: listener -> rx_task reassembles
// segments and re-frames each uplink toward Aerial. TX: be-pack -> segment ->
// send toward the VNF. The nFAPI wire is big-endian (be_* codec).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "oai_pnf.h"

#ifdef AERIAL_OAI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../../main/app_context.h"
#include "aerial_oai_bridge.h"
#include "nfapi_codec_be.h"
#include "unified_logger.h"
#include "itc_queue.h"
#include "nfapi_interface.h"
#include "nfapi_nr_interface.h"
#include "nfapi_nr_interface_scf.h"

// ===========================================================================
// RX buffer pool
// ===========================================================================

void oai_pnf_rx_pool_init(oai_pnf_rx_pool_t* pool)
{
    for (int i = 0; i < OAI_PNF_P7_RX_POOL_SLOTS; ++i) {
        atomic_store(&pool->slots[i].in_use, 0);
        pool->slots[i].length = 0;
    }
    atomic_store(&pool->next_hint, 0);
    atomic_store(&pool->acquire_fail, 0);
}

oai_pnf_rx_slot_t* oai_pnf_rx_pool_acquire(oai_pnf_rx_pool_t* pool)
{
    unsigned start = atomic_load(&pool->next_hint);
    for (int i = 0; i < OAI_PNF_P7_RX_POOL_SLOTS; ++i) {
        unsigned idx = (start + (unsigned)i) % OAI_PNF_P7_RX_POOL_SLOTS;
        int expected = 0;
        if (atomic_compare_exchange_strong(&pool->slots[idx].in_use,
                                           &expected, 1)) {
            atomic_store(&pool->next_hint, (idx + 1u) % OAI_PNF_P7_RX_POOL_SLOTS);
            return &pool->slots[idx];
        }
    }
    atomic_fetch_add(&pool->acquire_fail, 1);
    return NULL;
}

void oai_pnf_rx_pool_release(oai_pnf_rx_pool_t* pool, oai_pnf_rx_slot_t* slot)
{
    (void)pool;
    if (slot == NULL) return;
    slot->length = 0;
    atomic_store(&slot->in_use, 0);
}

// ===========================================================================
// Segmentation reassembly
// ===========================================================================

void oai_pnf_seg_queue_init(oai_pnf_seg_queue_t* q)
{
    pthread_mutex_init(&q->mutex, NULL);
    memset(q->entries, 0, sizeof(q->entries));
}

static void seg_entry_clear(oai_pnf_seq_entry_t* e)
{
    for (int i = 0; i < OAI_PNF_P7_MAX_SEGMENTS; ++i) {
        if (e->seg_buf[i]) {
            free(e->seg_buf[i]);
            e->seg_buf[i] = NULL;
        }
        e->seg_len[i] = 0;
    }
    e->active = 0;
    e->more_pending = 0;
    e->highest_segment = -1;
    e->have_last = 0;
    e->segments_present = 0;
    e->sequence = 0;
}

void oai_pnf_seg_queue_reset(oai_pnf_seg_queue_t* q)
{
    pthread_mutex_lock(&q->mutex);
    for (int i = 0; i < OAI_PNF_P7_MAX_SEQUENCES; ++i) {
        seg_entry_clear(&q->entries[i]);
    }
    pthread_mutex_unlock(&q->mutex);
}

void oai_pnf_seg_queue_destroy(oai_pnf_seg_queue_t* q)
{
    oai_pnf_seg_queue_reset(q);
    pthread_mutex_destroy(&q->mutex);
}

static oai_pnf_seq_entry_t* seg_find_or_alloc(oai_pnf_seg_queue_t* q, uint8_t seq)
{
    for (int i = 0; i < OAI_PNF_P7_MAX_SEQUENCES; ++i) {
        if (q->entries[i].active && q->entries[i].sequence == seq) {
            return &q->entries[i];
        }
    }
    for (int i = 0; i < OAI_PNF_P7_MAX_SEQUENCES; ++i) {
        if (!q->entries[i].active) {
            seg_entry_clear(&q->entries[i]);
            q->entries[i].active = 1;
            q->entries[i].sequence = seq;
            q->entries[i].highest_segment = -1;
            return &q->entries[i];
        }
    }
    return NULL;
}

// ===========================================================================
// Dispatch a fully-reassembled P7 message toward Aerial.
// ===========================================================================

static void oai_pnf_p7_dispatch(AppContext* ctx, const uint8_t* msg, uint32_t len)
{
    if (len < NFAPI_NR_P7_HEADER_LENGTH) {
        SM_Logs(LOG_WARN, _P7_, "[OAI_PNF] P7 message too short (%u); dropping.", len);
        return;
    }
    (void)aerial_oai_bridge_p7_to_aerial(ctx, msg, len);
}

static void oai_pnf_p7_handle_segmented(oai_pnf_t* p, const uint8_t* msg,
                                        uint32_t len)
{
    nfapi_nr_p7_message_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!be_nfapi_nr_p7_message_header_unpack((void*)msg, len, &hdr, sizeof(hdr),
                                              NULL)) {
        SM_Logs(LOG_ERROR, _P7_, "[OAI_PNF] seg header unpack failed; dropping.");
        return;
    }

    uint8_t more    = NFAPI_P7_GET_MORE(hdr.m_segment_sequence);
    uint8_t segment = NFAPI_P7_GET_SEGMENT(hdr.m_segment_sequence);
    uint8_t seqn    = NFAPI_P7_GET_SEQUENCE(hdr.m_segment_sequence);

    if (segment >= OAI_PNF_P7_MAX_SEGMENTS) {
        SM_Logs(LOG_WARN, _P7_,
                "[OAI_PNF] segment %u exceeds max %d; dropping seq=%u.",
                segment, OAI_PNF_P7_MAX_SEGMENTS, seqn);
        return;
    }

    pthread_mutex_lock(&p->seg_queue.mutex);
    oai_pnf_seq_entry_t* e = seg_find_or_alloc(&p->seg_queue, seqn);
    if (e == NULL) {
        pthread_mutex_unlock(&p->seg_queue.mutex);
        SM_Logs(LOG_WARN, _P7_,
                "[OAI_PNF] reassembly table full; dropping seq=%u seg=%u.",
                seqn, segment);
        return;
    }

    if (e->seg_buf[segment] == NULL) {
        e->seg_buf[segment] = (uint8_t*)malloc(len);
        if (e->seg_buf[segment] == NULL) {
            pthread_mutex_unlock(&p->seg_queue.mutex);
            SM_Logs(LOG_ERROR, _P7_, "[OAI_PNF] seg malloc(%u) failed.", len);
            return;
        }
        memcpy(e->seg_buf[segment], msg, len);
        e->seg_len[segment] = len;
        e->segments_present++;
        if ((int)segment > e->highest_segment) e->highest_segment = segment;
    }
    if (!more) e->have_last = 1;

    int expected = e->highest_segment + 1;
    if (!(e->have_last && e->segments_present == expected)) {
        pthread_mutex_unlock(&p->seg_queue.mutex);
        return;
    }

    uint32_t total = e->seg_len[0];
    for (int i = 1; i < expected; ++i) {
        if (e->seg_len[i] < NFAPI_NR_P7_HEADER_LENGTH) {
            pthread_mutex_unlock(&p->seg_queue.mutex);
            SM_Logs(LOG_ERROR, _P7_,
                    "[OAI_PNF] seg %d too short for header; dropping seq=%u.",
                    i, seqn);
            seg_entry_clear(e);
            return;
        }
        total += e->seg_len[i] - NFAPI_NR_P7_HEADER_LENGTH;
    }

    uint8_t* full = (uint8_t*)malloc(total);
    if (full == NULL) {
        pthread_mutex_unlock(&p->seg_queue.mutex);
        SM_Logs(LOG_ERROR, _P7_, "[OAI_PNF] reassembly malloc(%u) failed.", total);
        seg_entry_clear(e);
        return;
    }
    uint32_t off = 0;
    memcpy(full + off, e->seg_buf[0], e->seg_len[0]);
    off += e->seg_len[0];
    for (int i = 1; i < expected; ++i) {
        uint32_t payload = e->seg_len[i] - NFAPI_NR_P7_HEADER_LENGTH;
        memcpy(full + off, e->seg_buf[i] + NFAPI_NR_P7_HEADER_LENGTH, payload);
        off += payload;
    }
    seg_entry_clear(e);
    pthread_mutex_unlock(&p->seg_queue.mutex);

    oai_pnf_p7_dispatch(p->ctx, full, total);
    free(full);
}

// ===========================================================================
// P7 RX threads
// ===========================================================================

static void* oai_pnf_p7_listener_thread(void* arg)
{
    oai_pnf_t* p = (oai_pnf_t*)arg;
    AppContext* ctx = p->ctx;

    while (atomic_load(&p->running)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(p->p7_sock, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int rc = select(p->p7_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            SM_Logs(LOG_WARN, _P7_, "[OAI_PNF] P7 select() failed: %s",
                    strerror(errno));
            continue;
        }
        if (rc == 0 || !FD_ISSET(p->p7_sock, &rfds)) {
            continue;
        }

        uint8_t peek[NFAPI_NR_P7_HEADER_LENGTH];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int pk = recvfrom(p->p7_sock, peek, sizeof(peek),
                          MSG_PEEK | MSG_DONTWAIT,
                          (struct sockaddr*)&from, &fromlen);
        if (pk <= 0) {
            continue;
        }
        if (pk < (int)NFAPI_NR_P7_HEADER_LENGTH) {
            (void)recvfrom(p->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_PNF] P7 runt datagram (%d bytes); discarded.", pk);
            continue;
        }

        nfapi_nr_p7_message_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        if (!be_nfapi_nr_p7_message_header_unpack(peek, sizeof(peek),
                                                  &hdr, sizeof(hdr), NULL)) {
            (void)recvfrom(p->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_ERROR, _P7_, "[OAI_PNF] P7 peek header unpack failed.");
            continue;
        }

        uint32_t mlen = hdr.message_length;
        if (mlen < NFAPI_NR_P7_HEADER_LENGTH || mlen > OAI_PNF_P7_RX_SLOT_BYTES) {
            (void)recvfrom(p->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_PNF] P7 implausible message_length=%u; discarded.", mlen);
            continue;
        }

        oai_pnf_rx_slot_t* slot = oai_pnf_rx_pool_acquire(&p->rx_pool);
        if (slot == NULL) {
            (void)recvfrom(p->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_PNF] P7 RX pool exhausted (fail #%u); dropping a slot.",
                    atomic_load(&p->rx_pool.acquire_fail));
            continue;
        }

        int got = recvfrom(p->p7_sock, slot->data, mlen,
                           MSG_WAITALL | MSG_TRUNC,
                           (struct sockaddr*)&from, &fromlen);
        if (got <= 0 || (uint32_t)got != mlen) {
            if (got > 0 && (uint32_t)got != mlen) {
                SM_Logs(LOG_WARN, _P7_,
                        "[OAI_PNF] P7 read %d != declared %u; dropping.", got, mlen);
            }
            oai_pnf_rx_pool_release(&p->rx_pool, slot);
            continue;
        }
        slot->length = mlen;

        oai_pnf_rx_item_t* item = (oai_pnf_rx_item_t*)malloc(sizeof(*item));
        if (item == NULL) {
            oai_pnf_rx_pool_release(&p->rx_pool, slot);
            SM_Logs(LOG_ERROR, _P7_, "[OAI_PNF] P7 rx item malloc failed.");
            continue;
        }
        item->slot   = slot;
        item->length = mlen;
        itc_queue_push(&ctx->aerial_oai_ctx.p7_rx_queue, item);
    }
    return NULL;
}

static void* oai_pnf_p7_rx_task_thread(void* arg)
{
    oai_pnf_t* p = (oai_pnf_t*)arg;
    AppContext* ctx = p->ctx;

    while (atomic_load(&p->running)) {
        oai_pnf_rx_item_t* item =
            (oai_pnf_rx_item_t*)itc_queue_pop(&ctx->aerial_oai_ctx.p7_rx_queue);
        if (item == NULL) {
            break;  // queue deactivated during shutdown
        }

        const uint8_t* data = item->slot->data;
        uint32_t len = item->length;

        if (len >= NFAPI_NR_P7_HEADER_LENGTH) {
            nfapi_nr_p7_message_header_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            if (be_nfapi_nr_p7_message_header_unpack((void*)data, len,
                                                     &hdr, sizeof(hdr), NULL)) {
                uint8_t more    = NFAPI_P7_GET_MORE(hdr.m_segment_sequence);
                uint8_t segment = NFAPI_P7_GET_SEGMENT(hdr.m_segment_sequence);
                if (more == 0 && segment == 0) {
                    oai_pnf_p7_dispatch(ctx, data, len);
                } else {
                    oai_pnf_p7_handle_segmented(p, data, len);
                }
            } else {
                SM_Logs(LOG_ERROR, _P7_,
                        "[OAI_PNF] rx_task header unpack failed; dropping.");
            }
        } else {
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_PNF] rx_task short msg (%u); dropping.", len);
        }

        oai_pnf_rx_pool_release(&p->rx_pool, item->slot);
        free(item);
    }
    return NULL;
}

int oai_pnf_p7_threads_start(oai_pnf_t* p)
{
    AppContext* ctx = p->ctx;

    itc_queue_init(&ctx->aerial_oai_ctx.p7_rx_queue);

    int rc = pthread_create(&p->p7_listener_tid, NULL,
                            oai_pnf_p7_listener_thread, p);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _P7_,
                "[OAI_PNF] pthread_create(p7_listener) failed: %d", rc);
        return -1;
    }
    rc = pthread_create(&p->rx_task_tid, NULL, oai_pnf_p7_rx_task_thread, p);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _P7_,
                "[OAI_PNF] pthread_create(rx_task) failed: %d", rc);
        itc_queue_destroy(&ctx->aerial_oai_ctx.p7_rx_queue);
        pthread_join(p->p7_listener_tid, NULL);
        return -1;
    }

    int recv_core = ctx->config.forwarder.recv_core_id;
    int send_core = ctx->config.forwarder.send_core_id;
    if (recv_core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(recv_core, &set);
        (void)pthread_setaffinity_np(p->p7_listener_tid, sizeof(set), &set);
    }
    if (send_core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(send_core, &set);
        (void)pthread_setaffinity_np(p->rx_task_tid, sizeof(set), &set);
    }
    return 0;
}

void oai_pnf_p7_threads_stop(oai_pnf_t* p)
{
    AppContext* ctx = p->ctx;
    itc_queue_destroy(&ctx->aerial_oai_ctx.p7_rx_queue);
    pthread_join(p->p7_listener_tid, NULL);
    pthread_join(p->rx_task_tid, NULL);
}

// ===========================================================================
// P7 send path (Aerial -> OAI): be-pack -> segment -> sendto
// ===========================================================================

static int oai_pnf_p7_sendto(oai_pnf_t* p, const uint8_t* buf, uint32_t len)
{
    if (!p->p7_addr_known) {
        SM_Logs(LOG_WARN, _P7_,
                "[OAI_PNF] P7 dest address not yet known; dropping %u bytes.", len);
        return -1;
    }
    int sent = sendto(p->p7_sock, buf, len, 0,
                      (struct sockaddr*)&p->vnf_p7_addr, sizeof(p->vnf_p7_addr));
    if (sent != (int)len) {
        SM_Logs(LOG_ERROR, _P7_, "[OAI_PNF] P7 sendto %d/%u: %s",
                sent, len, strerror(errno));
        return -1;
    }
    return 0;
}

int oai_pnf_send_p7(struct AppContext* ctx, nfapi_nr_p7_message_header_t* header)
{
    if (ctx == NULL || ctx->aerial_oai_ctx.pnf == NULL || header == NULL) {
        return -1;
    }
    oai_pnf_t* p = ctx->aerial_oai_ctx.pnf;

    header->m_segment_sequence =
        NFAPI_NR_P7_SET_MSS(0, 0, (uint8_t)(p->p7_sequence & 0xFF));

    uint8_t buf[OAI_PNF_TX_BUF_SIZE];
    int len = be_nfapi_nr_p7_message_pack(header, buf, sizeof(buf), &p->p7_codec);
    if (len < 0) {
        SM_Logs(LOG_ERROR, _P7_,
                "[OAI_PNF] p7_message_pack failed (rc=%d, msg_id=0x%04x).",
                len, header->message_id);
        return -1;
    }

    int rc = 0;
    if (len <= OAI_PNF_P7_SEGMENT_SIZE) {
        if (p->checksum_enabled) {
            be_nfapi_nr_p7_update_checksum(buf, (uint32_t)len);
        }
        rc = oai_pnf_p7_sendto(p, buf, (uint32_t)len);
    } else {
        int body_len = len - NFAPI_NR_P7_HEADER_LENGTH;
        int seg_body = OAI_PNF_P7_SEGMENT_SIZE - NFAPI_NR_P7_HEADER_LENGTH;
        int seg_count = (body_len + seg_body - 1) / seg_body;

        uint8_t seg[OAI_PNF_P7_SEGMENT_SIZE];
        int offset = NFAPI_NR_P7_HEADER_LENGTH;
        for (int s = 0; s < seg_count; ++s) {
            int last = (s + 1 == seg_count);
            int size = last ? (body_len - seg_body * s) : seg_body;
            uint32_t seg_total = (uint32_t)(size + NFAPI_NR_P7_HEADER_LENGTH);

            memcpy(seg, buf, NFAPI_NR_P7_HEADER_LENGTH);
            // NR P7 header: message_length big-endian u32 @ 4-7, m_segment_
            // sequence big-endian u16 @ 8-9 (must match the OAI VNF's socket).
            seg[4] = (uint8_t)((seg_total >> 24) & 0xFF);
            seg[5] = (uint8_t)((seg_total >> 16) & 0xFF);
            seg[6] = (uint8_t)((seg_total >> 8)  & 0xFF);
            seg[7] = (uint8_t)( seg_total        & 0xFF);
            uint16_t mss = NFAPI_NR_P7_SET_MSS(last ? 0 : 1, (uint8_t)s,
                                               (uint8_t)(p->p7_sequence & 0xFF));
            seg[8] = (uint8_t)((mss >> 8) & 0xFF);
            seg[9] = (uint8_t)( mss       & 0xFF);

            memcpy(seg + NFAPI_NR_P7_HEADER_LENGTH, buf + offset, (size_t)size);
            offset += size;

            if (p->checksum_enabled) {
                be_nfapi_nr_p7_update_checksum(seg, seg_total);
            }
            if (oai_pnf_p7_sendto(p, seg, seg_total) != 0) {
                rc = -1;
                break;
            }
        }
    }

    p->p7_sequence++;
    return rc;
}

#endif /* AERIAL_OAI */
