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

// xFAPI as nFAPI PNF (server); OAI L2 (MAC) is the VNF. Owns the P5 (SCTP)
// handshake responder + cell-P5 relay and the P7 (UDP) data plane.

#ifndef AERIAL_OAI_OAI_PNF_H
#define AERIAL_OAI_OAI_PNF_H

#ifdef AERIAL_OAI

#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <netinet/in.h>

#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"

struct AppContext;

#define OAI_PNF_TX_BUF_SIZE       NFAPI_MAX_PACKED_MESSAGE_SIZE
#define OAI_PNF_P7_SEGMENT_SIZE   1400
#define OAI_PNF_P7_RX_POOL_SLOTS  256
#define OAI_PNF_P7_RX_SLOT_BYTES  (64 * 1024)
#define OAI_PNF_P7_MAX_SEQUENCES  8
#define OAI_PNF_P7_MAX_SEGMENTS   128
#define OAI_PNF_SCTP_BACKLOG      5
#define OAI_PNF_SCTP_STREAMS      5

typedef enum {
    OAI_PNF_DISCONNECTED = 0,
    OAI_PNF_CONNECTED,
    OAI_PNF_CONFIGURED,
    OAI_PNF_RUNNING
} oai_pnf_state_t;

typedef struct {
    uint8_t  data[OAI_PNF_P7_RX_SLOT_BYTES];
    uint32_t length;
    atomic_int in_use;
} oai_pnf_rx_slot_t;

typedef struct {
    oai_pnf_rx_slot_t slots[OAI_PNF_P7_RX_POOL_SLOTS];
    atomic_uint       next_hint;
    atomic_uint       acquire_fail;
} oai_pnf_rx_pool_t;

typedef struct {
    oai_pnf_rx_slot_t* slot;
    uint32_t           length;
} oai_pnf_rx_item_t;

typedef struct {
    int      active;
    uint8_t  sequence;
    int      more_pending;
    int      highest_segment;
    int      have_last;
    int      segments_present;
    uint32_t seg_len[OAI_PNF_P7_MAX_SEGMENTS];
    uint8_t* seg_buf[OAI_PNF_P7_MAX_SEGMENTS];
} oai_pnf_seq_entry_t;

typedef struct {
    pthread_mutex_t     mutex;
    oai_pnf_seq_entry_t entries[OAI_PNF_P7_MAX_SEQUENCES];
} oai_pnf_seg_queue_t;

typedef struct oai_pnf {
    struct AppContext* ctx;

    int                 listener_fd;
    int                 p5_sock;
    struct sockaddr_in  vnf_p5_addr;
    oai_pnf_state_t     state;
    pthread_t           p5_listener_tid;
    int                 p5_listener_started;

    int                 p7_sock;
    struct sockaddr_in  vnf_p7_addr;
    int                 p7_addr_known;
    int                 p7_sequence;

    nfapi_p4_p5_codec_config_t p5_codec;
    nfapi_p7_codec_config_t    p7_codec;

    uint8_t             p5_tx_buf[OAI_PNF_TX_BUF_SIZE];

    oai_pnf_rx_pool_t   rx_pool;
    oai_pnf_seg_queue_t seg_queue;
    pthread_t           p7_listener_tid;
    pthread_t           rx_task_tid;
    int                 p7_threads_started;

    atomic_int          running;
    int                 checksum_enabled;
} oai_pnf_t;

int  oai_pnf_start(struct AppContext* ctx);

void oai_pnf_stop(struct AppContext* ctx);

int  oai_pnf_send_p5(struct AppContext* ctx,
                     nfapi_nr_p4_p5_message_header_t* hdr, uint32_t msg_len);

int  oai_pnf_send_p7(struct AppContext* ctx, nfapi_nr_p7_message_header_t* header);

int  aerial_oai_from_aerial(struct AppContext* ctx, int32_t msg_id,
                            const uint8_t* scf_msg, uint32_t scf_len,
                            const uint8_t* data_buf, uint32_t data_len);

void oai_pnf_rx_pool_init(oai_pnf_rx_pool_t* pool);
oai_pnf_rx_slot_t* oai_pnf_rx_pool_acquire(oai_pnf_rx_pool_t* pool);
void oai_pnf_rx_pool_release(oai_pnf_rx_pool_t* pool, oai_pnf_rx_slot_t* slot);
void oai_pnf_seg_queue_init(oai_pnf_seg_queue_t* q);
void oai_pnf_seg_queue_reset(oai_pnf_seg_queue_t* q);
void oai_pnf_seg_queue_destroy(oai_pnf_seg_queue_t* q);
int  oai_pnf_p7_threads_start(oai_pnf_t* p);
void oai_pnf_p7_threads_stop(oai_pnf_t* p);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_OAI_PNF_H */
