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

// OAI_OCUDU VNF: xFAPI as nFAPI VNF (server); OAI L1 is the PNF (client). Owns
// the P5 (SCTP) handshake and P7 (UDP) data plane via the nFAPI codec API.

#ifndef OAI_OCUDU_OAI_VNF_H
#define OAI_OCUDU_OAI_VNF_H

#ifdef OAI_OCUDU

#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <netinet/in.h>

#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"

struct AppContext;

// ---------------------------------------------------------------------------
// Tunables (protocol / sizing constants).
// ---------------------------------------------------------------------------

// Max packed P5/P7 message we will pack into a single TX buffer.
#define OAI_VNF_TX_BUF_SIZE        NFAPI_MAX_PACKED_MESSAGE_SIZE

// P7 segmentation: max UDP payload per segment on the wire.
#define OAI_VNF_P7_SEGMENT_SIZE    1400

// Preallocated P7 RX buffer pool: fixed ring of reusable buffers so the
// hot path never malloc/free's per datagram.
#define OAI_VNF_P7_RX_POOL_SLOTS   256
#define OAI_VNF_P7_RX_SLOT_BYTES   (64 * 1024)

// P7 segmentation reassembly: parallel in-flight sequences and max segments.
#define OAI_VNF_P7_MAX_SEQUENCES   8
#define OAI_VNF_P7_MAX_SEGMENTS    128

// SCTP listen backlog and stream count.
#define OAI_VNF_SCTP_BACKLOG       5
#define OAI_VNF_SCTP_STREAMS       5

// ---------------------------------------------------------------------------
// PNF connection lifecycle state.
// ---------------------------------------------------------------------------
typedef enum {
    OAI_VNF_PNF_DISCONNECTED = 0,
    OAI_VNF_PNF_CONNECTED,            // SCTP accepted, P5 up
    OAI_VNF_PNF_PNF_PARAM_SENT,
    OAI_VNF_PNF_PNF_CONFIG_SENT,
    OAI_VNF_PNF_PNF_START_SENT,
    OAI_VNF_PNF_RUNNING               // PNF_START.response received, P7 active
} oai_vnf_pnf_state_t;

// ---------------------------------------------------------------------------
// One preallocated P7 RX buffer slot.
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  data[OAI_VNF_P7_RX_SLOT_BYTES];
    uint32_t length;                  // valid bytes when in flight
    atomic_int in_use;                // 0 = free, 1 = acquired
} oai_p7_rx_slot_t;

typedef struct {
    oai_p7_rx_slot_t slots[OAI_VNF_P7_RX_POOL_SLOTS];
    atomic_uint      next_hint;       // round-robin search hint
    atomic_uint      acquire_fail;    // stat: pool-exhausted count
} oai_p7_rx_pool_t;

// A P7 RX item handed from the listener thread to the rx_task via the queue.
typedef struct {
    oai_p7_rx_slot_t* slot;           // borrowed pool slot (released by rx_task)
    uint32_t          length;
} oai_p7_rx_item_t;

// ---------------------------------------------------------------------------
// P7 segmentation reassembly (mutex-protected).
// ---------------------------------------------------------------------------
typedef struct {
    int      active;
    uint8_t  sequence;
    int      more_pending;            // 1 while MORE flag still set
    int      highest_segment;         // largest segment index seen
    int      have_last;               // last segment (MORE=0) seen
    int      segments_present;        // count of distinct segments stored
    uint32_t seg_len[OAI_VNF_P7_MAX_SEGMENTS];
    uint8_t* seg_buf[OAI_VNF_P7_MAX_SEGMENTS];
} oai_p7_seq_entry_t;

typedef struct {
    pthread_mutex_t    mutex;
    oai_p7_seq_entry_t entries[OAI_VNF_P7_MAX_SEQUENCES];
} oai_p7_seg_queue_t;

// ---------------------------------------------------------------------------
// VNF runtime state. Allocated once in oai_vnf_start(), owned by AppContext
// (ctx->oai_ocudu_ctx.vnf), freed in oai_vnf_stop().
// ---------------------------------------------------------------------------
typedef struct oai_vnf {
    struct AppContext* ctx;           // back-pointer

    // ---- P5 (SCTP) ----
    int                 listener_fd;  // SCTP listening socket
    int                 p5_sock;      // accepted PNF connection (-1 if none)
    struct sockaddr_in  pnf_p5_addr;  // peer (PNF) P5 address
    oai_vnf_pnf_state_t pnf_state;
    pthread_t           p5_listener_tid;
    int                 p5_listener_started;

    // ---- P7 (UDP) ----
    int                 p7_sock;      // UDP socket bound to p7_local_port
    struct sockaddr_in  pnf_p7_addr;  // PNF P7 dest (from PARAM.resp or config)
    int                 p7_addr_known;

    // ---- handshake / sequence ----
    int                 phy_id;
    int                 p7_sequence;  // P7 message sequence counter
    int                 cell_numerology;  // mu (scs_common) latched from CONFIG.request;
                                          // used to encode OCUDU slot_point in SLOT.indication
    int                 cell_pci;         // phys_cell_id latched from CONFIG.request;
                                          // OCUDU's UL PRACH PDU omits it, nFAPI needs it

    // ---- codec configs (passed to pack/unpack) ----
    nfapi_p4_p5_codec_config_t p5_codec;
    nfapi_p7_codec_config_t    p7_codec;

    // ---- TX buffer for P5 (P5 thread is single-threaded sender) ----
    uint8_t             p5_tx_buf[OAI_VNF_TX_BUF_SIZE];

    // ---- P7 RX path ----
    oai_p7_rx_pool_t    rx_pool;
    oai_p7_seg_queue_t  seg_queue;
    pthread_t           p7_listener_tid;
    pthread_t           rx_task_tid;
    int                 p7_threads_started;

    // ---- run control ----
    atomic_int          running;      // master run flag for all VNF threads

    // ---- checksum toggle (from config) ----
    int                 checksum_enabled;
} oai_vnf_t;

// ---------------------------------------------------------------------------
// Public API (called from oai_l1_interface.c).
// ---------------------------------------------------------------------------

// Allocate + initialize VNF state, create+bind the SCTP P5 listener and the
// P7 UDP socket, and start the P5 listener thread. The listener accepts the
// PNF (OAI L1), drives the PNF handshake, and (on PNF_START.response) starts
// the P7 listener + rx_task. Returns 0 on success, -1 on failure.
int  oai_vnf_start(struct AppContext* ctx);

// Stop all VNF threads, close sockets, free state. Idempotent.
void oai_vnf_stop(struct AppContext* ctx);

// Send a fully-populated P7 message to the PNF (pack -> segment -> sendto).
// Used by the L2->OAI direction. Returns 0 on success, -1 on failure.
int  oai_vnf_send_p7(struct AppContext* ctx, nfapi_p7_message_header_t* header);

// Pack a fully-populated nFAPI P5 message (nfapi_p5_hdr points at the message's
// nfapi_nr_p4_p5_message_header_t; msg_len is sizeof the full message struct)
// and send it to the PNF over SCTP. Public wrapper used by the OCUDU->nFAPI
// P5 translators. Returns 0 on success, -1 on failure.
int  oai_vnf_send_p5_msg(struct oai_vnf* v, void* nfapi_p5_hdr, uint32_t msg_len);

// Current PNF handshake state (read by the L2->OAI forwarder to gate cell-level
// P5 sends until the PNF link is RUNNING).
oai_vnf_pnf_state_t oai_vnf_get_pnf_state(struct oai_vnf* v);

#endif /* OAI_OCUDU */
#endif /* OAI_OCUDU_OAI_VNF_H */
