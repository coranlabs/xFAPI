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

// OAI_OCUDU VNF — P7 (UDP) data plane. RX: listener -> rx_task reassembles
// segments and translates uplink toward OCUDU L2. TX: pack -> segment -> send.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "oai_p7.h"

#ifdef OAI_OCUDU

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
#include "oai_l1_to_l2_p7.h"
#include "unified_logger.h"
#include "nfapi_interface.h"
#include "nfapi_nr_interface.h"
#include "nfapi_nr_interface_scf.h"

// ===========================================================================
// RX buffer pool
// ===========================================================================

void oai_p7_rx_pool_init(oai_p7_rx_pool_t* pool)
{
    for (int i = 0; i < OAI_VNF_P7_RX_POOL_SLOTS; ++i) {
        atomic_store(&pool->slots[i].in_use, 0);
        pool->slots[i].length = 0;
    }
    atomic_store(&pool->next_hint, 0);
    atomic_store(&pool->acquire_fail, 0);
}

oai_p7_rx_slot_t* oai_p7_rx_pool_acquire(oai_p7_rx_pool_t* pool)
{
    unsigned start = atomic_load(&pool->next_hint);
    for (int i = 0; i < OAI_VNF_P7_RX_POOL_SLOTS; ++i) {
        unsigned idx = (start + (unsigned)i) % OAI_VNF_P7_RX_POOL_SLOTS;
        int expected = 0;
        if (atomic_compare_exchange_strong(&pool->slots[idx].in_use,
                                           &expected, 1)) {
            atomic_store(&pool->next_hint,
                         (idx + 1u) % OAI_VNF_P7_RX_POOL_SLOTS);
            return &pool->slots[idx];
        }
    }
    atomic_fetch_add(&pool->acquire_fail, 1);
    return NULL;
}

void oai_p7_rx_pool_release(oai_p7_rx_pool_t* pool, oai_p7_rx_slot_t* slot)
{
    (void)pool;
    if (slot == NULL) return;
    slot->length = 0;
    atomic_store(&slot->in_use, 0);
}

// ===========================================================================
// Segmentation reassembly
// ===========================================================================

void oai_p7_seg_queue_init(oai_p7_seg_queue_t* q)
{
    pthread_mutex_init(&q->mutex, NULL);
    memset(q->entries, 0, sizeof(q->entries));
}

static void seg_entry_clear(oai_p7_seq_entry_t* e)
{
    for (int i = 0; i < OAI_VNF_P7_MAX_SEGMENTS; ++i) {
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

void oai_p7_seg_queue_reset(oai_p7_seg_queue_t* q)
{
    pthread_mutex_lock(&q->mutex);
    for (int i = 0; i < OAI_VNF_P7_MAX_SEQUENCES; ++i) {
        seg_entry_clear(&q->entries[i]);
    }
    pthread_mutex_unlock(&q->mutex);
}

void oai_p7_seg_queue_destroy(oai_p7_seg_queue_t* q)
{
    oai_p7_seg_queue_reset(q);
    pthread_mutex_destroy(&q->mutex);
}

// Find or allocate the entry for a sequence. Caller holds the mutex.
static oai_p7_seq_entry_t* seg_find_or_alloc(oai_p7_seg_queue_t* q, uint8_t seq)
{
    for (int i = 0; i < OAI_VNF_P7_MAX_SEQUENCES; ++i) {
        if (q->entries[i].active && q->entries[i].sequence == seq) {
            return &q->entries[i];
        }
    }
    for (int i = 0; i < OAI_VNF_P7_MAX_SEQUENCES; ++i) {
        if (!q->entries[i].active) {
            seg_entry_clear(&q->entries[i]);
            q->entries[i].active = 1;
            q->entries[i].sequence = seq;
            q->entries[i].highest_segment = -1;
            return &q->entries[i];
        }
    }
    return NULL;  // table full
}

// ===========================================================================
// P7 message dispatch (a fully-reassembled message)
// ===========================================================================

static void oai_p7_dispatch(AppContext* ctx, const uint8_t* msg, uint32_t len)
{
    if (len < NFAPI_NR_P7_HEADER_LENGTH) {
        SM_Logs(LOG_WARN, _P7_, "[OAI_VNF] P7 message too short (%u); dropping.", len);
        return;
    }

    // MUST use the NR header unpack: the legacy one reads message_length as a
    // u16 and yields 0 for NR messages.
    nfapi_nr_p7_message_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!nfapi_nr_p7_message_header_unpack((void*)msg, len, &hdr, sizeof(hdr), NULL)) {
        SM_Logs(LOG_ERROR, _P7_, "[OAI_VNF] P7 header unpack failed; dropping.");
        return;
    }

    // Translate the nFAPI uplink/indication to OCUDU-FAPI and deliver to L2.
    (void)ocudu_p7_translate_to_l2(ctx, hdr.message_id, msg, len);
}

// Handle a possibly-segmented P7 message. Reassembles into a contiguous
// buffer and dispatches when complete.
static void oai_p7_handle_segmented(oai_vnf_t* v, const uint8_t* msg, uint32_t len)
{
    nfapi_nr_p7_message_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!nfapi_nr_p7_message_header_unpack((void*)msg, len, &hdr, sizeof(hdr), NULL)) {
        SM_Logs(LOG_ERROR, _P7_, "[OAI_VNF] seg header unpack failed; dropping.");
        return;
    }

    uint8_t more    = NFAPI_P7_GET_MORE(hdr.m_segment_sequence);
    uint8_t segment = NFAPI_P7_GET_SEGMENT(hdr.m_segment_sequence);
    uint8_t seqn    = NFAPI_P7_GET_SEQUENCE(hdr.m_segment_sequence);

    if (segment >= OAI_VNF_P7_MAX_SEGMENTS) {
        SM_Logs(LOG_WARN, _P7_,
                "[OAI_VNF] segment %u exceeds max %d; dropping seq=%u.",
                segment, OAI_VNF_P7_MAX_SEGMENTS, seqn);
        return;
    }

    pthread_mutex_lock(&v->seg_queue.mutex);
    oai_p7_seq_entry_t* e = seg_find_or_alloc(&v->seg_queue, seqn);
    if (e == NULL) {
        pthread_mutex_unlock(&v->seg_queue.mutex);
        SM_Logs(LOG_WARN, _P7_,
                "[OAI_VNF] reassembly table full; dropping seq=%u seg=%u.",
                seqn, segment);
        return;
    }

    if (e->seg_buf[segment] == NULL) {
        e->seg_buf[segment] = (uint8_t*)malloc(len);
        if (e->seg_buf[segment] == NULL) {
            pthread_mutex_unlock(&v->seg_queue.mutex);
            SM_Logs(LOG_ERROR, _P7_, "[OAI_VNF] seg malloc(%u) failed.", len);
            return;
        }
        memcpy(e->seg_buf[segment], msg, len);
        e->seg_len[segment] = len;
        e->segments_present++;
        if ((int)segment > e->highest_segment) e->highest_segment = segment;
    }
    if (!more) e->have_last = 1;

    // Complete when the last segment has arrived AND we have all of 0..last.
    int complete = 0;
    int expected = e->highest_segment + 1;
    if (e->have_last && e->segments_present == expected) {
        complete = 1;
    }

    if (!complete) {
        pthread_mutex_unlock(&v->seg_queue.mutex);
        return;
    }

    // Reassemble: segment 0 keeps header+payload; later segments drop header.
    uint32_t total = e->seg_len[0];
    for (int i = 1; i < expected; ++i) {
        if (e->seg_len[i] < NFAPI_NR_P7_HEADER_LENGTH) {
            pthread_mutex_unlock(&v->seg_queue.mutex);
            SM_Logs(LOG_ERROR, _P7_,
                    "[OAI_VNF] seg %d too short for header; dropping seq=%u.",
                    i, seqn);
            seg_entry_clear(e);
            return;
        }
        total += e->seg_len[i] - NFAPI_NR_P7_HEADER_LENGTH;
    }

    uint8_t* full = (uint8_t*)malloc(total);
    if (full == NULL) {
        pthread_mutex_unlock(&v->seg_queue.mutex);
        SM_Logs(LOG_ERROR, _P7_, "[OAI_VNF] reassembly malloc(%u) failed.", total);
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
    pthread_mutex_unlock(&v->seg_queue.mutex);

    oai_p7_dispatch(v->ctx, full, total);
    free(full);
}

// ===========================================================================
// P7 RX threads
// ===========================================================================

static void* oai_p7_listener_thread(void* arg)
{
    oai_vnf_t* v = (oai_vnf_t*)arg;
    AppContext* ctx = v->ctx;

    while (atomic_load(&v->running)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(v->p7_sock, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int rc = select(v->p7_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            SM_Logs(LOG_WARN, _P7_, "[OAI_VNF] P7 select() failed: %s",
                    strerror(errno));
            continue;
        }
        if (rc == 0 || !FD_ISSET(v->p7_sock, &rfds)) {
            continue;
        }

        // Peek the header to learn the datagram length.
        uint8_t peek[NFAPI_NR_P7_HEADER_LENGTH];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int pk = recvfrom(v->p7_sock, peek, sizeof(peek),
                          MSG_PEEK | MSG_DONTWAIT,
                          (struct sockaddr*)&from, &fromlen);
        if (pk <= 0) {
            continue;
        }
        if (pk < (int)NFAPI_NR_P7_HEADER_LENGTH) {
            // Drain the runt datagram so it doesn't wedge the socket.
            (void)recvfrom(v->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_VNF] P7 runt datagram (%d bytes); discarded.", pk);
            continue;
        }

        nfapi_nr_p7_message_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        if (!nfapi_nr_p7_message_header_unpack(peek, sizeof(peek),
                                               &hdr, sizeof(hdr), NULL)) {
            (void)recvfrom(v->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_ERROR, _P7_, "[OAI_VNF] P7 peek header unpack failed.");
            continue;
        }

        uint32_t mlen = hdr.message_length;
        if (mlen < NFAPI_NR_P7_HEADER_LENGTH || mlen > OAI_VNF_P7_RX_SLOT_BYTES) {
            (void)recvfrom(v->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_VNF] P7 implausible message_length=%u; discarded.", mlen);
            continue;
        }

        oai_p7_rx_slot_t* slot = oai_p7_rx_pool_acquire(&v->rx_pool);
        if (slot == NULL) {
            // Pool exhausted: drain the datagram and drop (logged, not silent).
            (void)recvfrom(v->p7_sock, peek, sizeof(peek), 0, NULL, NULL);
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_VNF] P7 RX pool exhausted (fail #%u); dropping a slot.",
                    atomic_load(&v->rx_pool.acquire_fail));
            continue;
        }

        int got = recvfrom(v->p7_sock, slot->data, mlen,
                           MSG_WAITALL | MSG_TRUNC,
                           (struct sockaddr*)&from, &fromlen);
        if (got <= 0) {
            oai_p7_rx_pool_release(&v->rx_pool, slot);
            continue;
        }
        if ((uint32_t)got != mlen) {
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_VNF] P7 read %d != declared %u; dropping.", got, mlen);
            oai_p7_rx_pool_release(&v->rx_pool, slot);
            continue;
        }
        slot->length = mlen;

        oai_p7_rx_item_t* item = (oai_p7_rx_item_t*)malloc(sizeof(*item));
        if (item == NULL) {
            oai_p7_rx_pool_release(&v->rx_pool, slot);
            SM_Logs(LOG_ERROR, _P7_, "[OAI_VNF] P7 rx item malloc failed.");
            continue;
        }
        item->slot   = slot;
        item->length = mlen;
        itc_queue_push(&ctx->oai_ocudu_ctx.p7_rx_queue, item);
    }

    return NULL;
}

static void* oai_p7_rx_task_thread(void* arg)
{
    oai_vnf_t* v = (oai_vnf_t*)arg;
    AppContext* ctx = v->ctx;

    while (atomic_load(&v->running)) {
        oai_p7_rx_item_t* item =
            (oai_p7_rx_item_t*)itc_queue_pop(&ctx->oai_ocudu_ctx.p7_rx_queue);
        if (item == NULL) {
            // Queue deactivated during shutdown.
            break;
        }

        const uint8_t* data = item->slot->data;
        uint32_t len = item->length;

        if (len >= NFAPI_NR_P7_HEADER_LENGTH) {
            nfapi_p7_message_header_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            if (nfapi_p7_message_header_unpack((void*)data, len,
                                               &hdr, sizeof(hdr), NULL) >= 0) {
                uint8_t more    = NFAPI_P7_GET_MORE(hdr.m_segment_sequence);
                uint8_t segment = NFAPI_P7_GET_SEGMENT(hdr.m_segment_sequence);
                if (more == 0 && segment == 0) {
                    oai_p7_dispatch(ctx, data, len);
                } else {
                    oai_p7_handle_segmented(v, data, len);
                }
            } else {
                SM_Logs(LOG_ERROR, _P7_,
                        "[OAI_VNF] rx_task header unpack failed; dropping.");
            }
        } else {
            SM_Logs(LOG_WARN, _P7_,
                    "[OAI_VNF] rx_task short msg (%u); dropping.", len);
        }

        oai_p7_rx_pool_release(&v->rx_pool, item->slot);
        free(item);
    }

    return NULL;
}

int oai_p7_threads_start(oai_vnf_t* v)
{
    AppContext* ctx = v->ctx;

    // (Re)activate the RX handoff queue for this session.
    itc_queue_init(&ctx->oai_ocudu_ctx.p7_rx_queue);

    int rc = pthread_create(&v->p7_listener_tid, NULL,
                            oai_p7_listener_thread, v);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _P7_,
                "[OAI_VNF] pthread_create(p7_listener) failed: %d", rc);
        return -1;
    }
    rc = pthread_create(&v->rx_task_tid, NULL, oai_p7_rx_task_thread, v);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _P7_,
                "[OAI_VNF] pthread_create(rx_task) failed: %d", rc);
        // Stop the already-started listener before bailing.
        itc_queue_destroy(&ctx->oai_ocudu_ctx.p7_rx_queue);
        pthread_join(v->p7_listener_tid, NULL);
        return -1;
    }

    int recv_core = ctx->config.oai_forwarder.recv_core_id;
    int send_core = ctx->config.oai_forwarder.send_core_id;
    if (recv_core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(recv_core, &set);
        (void)pthread_setaffinity_np(v->p7_listener_tid, sizeof(set), &set);
    }
    if (send_core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(send_core, &set);
        (void)pthread_setaffinity_np(v->rx_task_tid, sizeof(set), &set);
    }

    return 0;
}

void oai_p7_threads_stop(oai_vnf_t* v)
{
    AppContext* ctx = v->ctx;

    // Deactivating the queue unblocks rx_task's itc_queue_pop.
    itc_queue_destroy(&ctx->oai_ocudu_ctx.p7_rx_queue);

    pthread_join(v->p7_listener_tid, NULL);
    pthread_join(v->rx_task_tid, NULL);
}

// ===========================================================================
// P7 send path (L2 -> OAI)
// ===========================================================================

static int oai_p7_sendto(oai_vnf_t* v, const uint8_t* buf, uint32_t len)
{
    if (!v->p7_addr_known) {
        SM_Logs(LOG_WARN, _P7_,
                "[OAI_VNF] P7 dest address not yet known; dropping %u bytes.", len);
        return -1;
    }
    int sent = sendto(v->p7_sock, buf, len, 0,
                      (struct sockaddr*)&v->pnf_p7_addr, sizeof(v->pnf_p7_addr));
    if (sent != (int)len) {
        SM_Logs(LOG_ERROR, _P7_, "[OAI_VNF] P7 sendto %d/%u: %s",
                sent, len, strerror(errno));
        return -1;
    }
    return 0;
}

int oai_vnf_send_p7(struct AppContext* ctx, nfapi_p7_message_header_t* header)
{
    if (ctx == NULL || ctx->oai_ocudu_ctx.vnf == NULL || header == NULL) {
        return -1;
    }
    oai_vnf_t* v = ctx->oai_ocudu_ctx.vnf;

    // Stamp the sequence (single segment to start; pack may grow it).
    header->m_segment_sequence =
        NFAPI_NR_P7_SET_MSS(0, 0, (uint8_t)(v->p7_sequence & 0xFF));

    uint8_t buf[OAI_VNF_TX_BUF_SIZE];
    int len = nfapi_nr_p7_message_pack(header, buf, sizeof(buf), &v->p7_codec);
    if (len < 0) {
        SM_Logs(LOG_ERROR, _P7_,
                "[OAI_VNF] nfapi_nr_p7_message_pack failed (rc=%d, msg_id=0x%04x).",
                len, header->message_id);
        return -1;
    }

    int rc = 0;
    if (len <= OAI_VNF_P7_SEGMENT_SIZE) {
        if (v->checksum_enabled) {
            nfapi_nr_p7_update_checksum(buf, (uint32_t)len);
        }
        rc = oai_p7_sendto(v, buf, (uint32_t)len);
    } else {
        // Segment: header carried on each segment, payload split.
        int body_len = len - NFAPI_NR_P7_HEADER_LENGTH;
        int seg_body = OAI_VNF_P7_SEGMENT_SIZE - NFAPI_NR_P7_HEADER_LENGTH;
        int seg_count = (body_len + seg_body - 1) / seg_body;

        uint8_t seg[OAI_VNF_P7_SEGMENT_SIZE];
        int offset = NFAPI_NR_P7_HEADER_LENGTH;
        for (int s = 0; s < seg_count; ++s) {
            int last = (s + 1 == seg_count);
            int size = last ? (body_len - seg_body * s) : seg_body;
            uint32_t seg_total = (uint32_t)(size + NFAPI_NR_P7_HEADER_LENGTH);

            memcpy(seg, buf, NFAPI_NR_P7_HEADER_LENGTH);
            // NR P7 header (must match OAI PNF socket_pnf.c): message_length is
            // big-endian u32 @ bytes 4-7; m_segment_sequence big-endian u16 @
            // bytes 8-9. (Legacy LTE layout would desync the PNF.)
            seg[4] = (uint8_t)((seg_total >> 24) & 0xFF);
            seg[5] = (uint8_t)((seg_total >> 16) & 0xFF);
            seg[6] = (uint8_t)((seg_total >> 8)  & 0xFF);
            seg[7] = (uint8_t)( seg_total        & 0xFF);
            uint16_t mss = NFAPI_NR_P7_SET_MSS(last ? 0 : 1, (uint8_t)s,
                                               (uint8_t)(v->p7_sequence & 0xFF));
            seg[8] = (uint8_t)((mss >> 8) & 0xFF);
            seg[9] = (uint8_t)( mss       & 0xFF);

            memcpy(seg + NFAPI_NR_P7_HEADER_LENGTH, buf + offset, (size_t)size);
            offset += size;

            if (v->checksum_enabled) {
                nfapi_nr_p7_update_checksum(seg, seg_total);
            }
            if (oai_p7_sendto(v, seg, seg_total) != 0) {
                rc = -1;
                break;
            }
        }
    }

    v->p7_sequence++;
    return rc;
}

#endif /* OAI_OCUDU */
