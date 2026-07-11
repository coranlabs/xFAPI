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

// AERIAL_OAI PNF: xFAPI as nFAPI PNF (server); OAI L2 (MAC) is the VNF (client).
// Owns the P5 (SCTP) handshake responder + cell-P5 relay and the P7 (UDP) data
// plane. The wire is big-endian nFAPI, decoded/encoded via the be_* codec.

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

// ---------------------------------------------------------------------------
// Tunables.
// ---------------------------------------------------------------------------
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
    OAI_PNF_CONNECTED,        // SCTP accepted, P5 up
    OAI_PNF_CONFIGURED,       // PNF_CONFIG.response sent
    OAI_PNF_RUNNING           // PNF_START.request seen; P7 active
} oai_pnf_state_t;

// ---- P7 RX buffer pool (preallocated) ----
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

// ---- P7 segmentation reassembly ----
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

// ---------------------------------------------------------------------------
// PNF runtime state. Allocated in oai_pnf_start(), owned by AppContext
// (ctx->aerial_oai_ctx.pnf), freed in oai_pnf_stop().
// ---------------------------------------------------------------------------
typedef struct oai_pnf {
    struct AppContext* ctx;

    // ---- P5 (SCTP) ----
    int                 listener_fd;
    int                 p5_sock;
    struct sockaddr_in  vnf_p5_addr;
    oai_pnf_state_t     state;
    pthread_t           p5_listener_tid;
    int                 p5_listener_started;

    // ---- P7 (UDP) ----
    int                 p7_sock;
    struct sockaddr_in  vnf_p7_addr;
    int                 p7_addr_known;
    int                 p7_sequence;

    // ---- codec configs ----
    nfapi_p4_p5_codec_config_t p5_codec;
    nfapi_p7_codec_config_t    p7_codec;

    uint8_t             p5_tx_buf[OAI_PNF_TX_BUF_SIZE];

    // ---- P7 RX path ----
    oai_pnf_rx_pool_t   rx_pool;
    oai_pnf_seg_queue_t seg_queue;
    pthread_t           p7_listener_tid;
    pthread_t           rx_task_tid;
    int                 p7_threads_started;

    atomic_int          running;
    int                 checksum_enabled;
} oai_pnf_t;

// ---------------------------------------------------------------------------
// Public API (framework/oai_l2_interface.c + the bridge).
// ---------------------------------------------------------------------------

// Start the PNF: SCTP P5 listener + UDP P7 socket + P5 listener thread. Returns
// 0 on success, -1 on failure.
int  oai_pnf_start(struct AppContext* ctx);

// Stop all PNF threads, close sockets, free state. Idempotent.
void oai_pnf_stop(struct AppContext* ctx);

// Pack a fully-populated nFAPI P5 message (big-endian) and send it to the VNF
// over SCTP. Used by the Aerial->OAI P5 bridge. Returns 0/-1.
int  oai_pnf_send_p5(struct AppContext* ctx,
                     nfapi_nr_p4_p5_message_header_t* hdr, uint32_t msg_len);

// Pack a fully-populated nFAPI P7 message (big-endian), segment if needed, and
// send it to the VNF over UDP. Used by the Aerial->OAI P7 bridge. Returns 0/-1.
int  oai_pnf_send_p7(struct AppContext* ctx, nfapi_nr_p7_message_header_t* header);

// Aerial -> OAI entry point, called by the nvIPC RX thread for every SCF FAPI
// message from Aerial. Routes P5 responses and P7 indications to the bridge.
// Returns 0/-1.
int  aerial_oai_from_aerial(struct AppContext* ctx, int32_t msg_id,
                            const uint8_t* scf_msg, uint32_t scf_len,
                            const uint8_t* data_buf, uint32_t data_len);

// ---- P7 socket data path (oai_p7.c) ----
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
