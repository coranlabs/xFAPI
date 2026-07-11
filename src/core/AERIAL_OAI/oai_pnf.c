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

// AERIAL_OAI PNF: xFAPI as the nFAPI PNF (server) toward OAI-L2 (the MAC/VNF).
// The socket role mirrors an nFAPI PNF: SCTP listen/accept for P5, UDP bind for
// P7. The handshake is the inverse of OAI_OCUDU's VNF — here xFAPI RESPONDS to
// PNF_PARAM/CONFIG/START.request with the matching .response and, once the VNF
// reaches the cell handshake, relays it to Aerial (P7 wiring lands with the
// data-plane bridge). The nFAPI wire is big-endian; encoded/decoded via be_*.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "oai_pnf.h"

#ifdef AERIAL_OAI

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "../../main/app_context.h"
#include "aerial_send.h"
#include "nfapi_codec_be.h"
#include "unified_logger.h"

#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"

#define OAI_PNF_TX_BUF_SIZE   NFAPI_MAX_PACKED_MESSAGE_SIZE
#define OAI_PNF_SCTP_BACKLOG  5
#define OAI_PNF_SCTP_STREAMS  5

typedef enum {
    OAI_PNF_DISCONNECTED = 0,
    OAI_PNF_CONNECTED,        // SCTP accepted, P5 up
    OAI_PNF_CONFIGURED,       // PNF_CONFIG.response sent
    OAI_PNF_RUNNING           // PNF_START.request seen; P7 active
} oai_pnf_state_t;

typedef struct oai_pnf {
    struct AppContext* ctx;

    // ---- P5 (SCTP) ----
    int                 listener_fd;
    int                 p5_sock;       // accepted VNF connection (-1 if none)
    struct sockaddr_in  vnf_p5_addr;   // peer (VNF/MAC) P5 address
    oai_pnf_state_t     state;
    pthread_t           p5_listener_tid;
    int                 p5_listener_started;

    // ---- P7 (UDP) ----
    int                 p7_sock;
    struct sockaddr_in  vnf_p7_addr;   // VNF P7 dest
    int                 p7_addr_known;

    // ---- codec configs ----
    nfapi_p4_p5_codec_config_t p5_codec;
    nfapi_p7_codec_config_t    p7_codec;

    uint8_t             p5_tx_buf[OAI_PNF_TX_BUF_SIZE];

    atomic_int          running;
    int                 checksum_enabled;
} oai_pnf_t;

static int  oai_pnf_open_p5_listener(oai_pnf_t* p);
static int  oai_pnf_open_p7_socket(oai_pnf_t* p);
static void oai_pnf_close_vnf_session(oai_pnf_t* p);
static void* oai_pnf_p5_listener_thread(void* arg);
static int  oai_pnf_accept_vnf(oai_pnf_t* p);
static int  oai_pnf_p5_read_dispatch(oai_pnf_t* p);
static void oai_pnf_handle_p5_message(oai_pnf_t* p, uint8_t* buf, uint32_t len);
static int  oai_pnf_send_p5(oai_pnf_t* p, nfapi_nr_p4_p5_message_header_t* hdr,
                            uint32_t msg_len);

// ===========================================================================
// Lifecycle
// ===========================================================================

int oai_pnf_start(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    oai_pnf_t* p = (oai_pnf_t*)calloc(1, sizeof(oai_pnf_t));
    if (p == NULL) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[OAI_PNF] calloc(oai_pnf_t) failed.");
        return -1;
    }
    p->ctx         = ctx;
    p->listener_fd = -1;
    p->p5_sock     = -1;
    p->p7_sock     = -1;
    p->state       = OAI_PNF_DISCONNECTED;
    p->checksum_enabled = ctx->config.nfapi_socket.checksum_enabled ? 1 : 0;
    atomic_store(&p->running, 1);

    memset(&p->p5_codec, 0, sizeof(p->p5_codec));
    memset(&p->p7_codec, 0, sizeof(p->p7_codec));

    if (oai_pnf_open_p7_socket(p) != 0) {
        free(p);
        return -1;
    }
    if (oai_pnf_open_p5_listener(p) != 0) {
        close(p->p7_sock);
        free(p);
        return -1;
    }

    ctx->aerial_oai_ctx.pnf = p;

    int core = ctx->config.forwarder.send_core_id;
    int rc = pthread_create(&p->p5_listener_tid, NULL,
                            oai_pnf_p5_listener_thread, p);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OAI_PNF] pthread_create(p5_listener) failed: %d", rc);
        close(p->listener_fd);
        close(p->p7_sock);
        ctx->aerial_oai_ctx.pnf = NULL;
        free(p);
        return -1;
    }
    p->p5_listener_started = 1;

    if (core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
        if (pthread_setaffinity_np(p->p5_listener_tid, sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OAI_PNF] P5 listener CPU pin to %d failed.", core);
        }
    }
    return 0;
}

void oai_pnf_stop(struct AppContext* ctx)
{
    if (ctx == NULL || ctx->aerial_oai_ctx.pnf == NULL) {
        return;
    }
    oai_pnf_t* p = ctx->aerial_oai_ctx.pnf;

    atomic_store(&p->running, 0);
    if (p->p5_listener_started) {
        pthread_join(p->p5_listener_tid, NULL);
        p->p5_listener_started = 0;
    }
    oai_pnf_close_vnf_session(p);
    if (p->listener_fd >= 0) { close(p->listener_fd); p->listener_fd = -1; }
    if (p->p7_sock >= 0)     { close(p->p7_sock);     p->p7_sock = -1; }

    ctx->aerial_oai_ctx.pnf = NULL;
    free(p);
}

// ===========================================================================
// Sockets
// ===========================================================================

static int oai_pnf_open_p5_listener(oai_pnf_t* p)
{
    const xFAPI_Config* cfg = &p->ctx->config;
    int port = cfg->nfapi_socket.p5_local_port;

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (fd < 0) {
        SM_Logs(LOG_CRTERR, _P5_,
                "[OAI_PNF] P5 socket(SCTP) failed: %s. Is the sctp kernel "
                "module loaded and libsctp installed?", strerror(errno));
        return -1;
    }
    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sctp_initmsg initmsg;
    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams  = OAI_PNF_SCTP_STREAMS;
    initmsg.sinit_max_instreams = OAI_PNF_SCTP_STREAMS;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(initmsg)) < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] setsockopt(SCTP_INITMSG) failed: %s",
                strerror(errno));
    }
    struct sctp_event_subscribe events;
    memset(&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_EVENTS, &events, sizeof(events)) < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] setsockopt(SCTP_EVENTS) failed: %s",
                strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        SM_Logs(LOG_CRTERR, _P5_, "[OAI_PNF] P5 bind(:%d) failed: %s",
                port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, OAI_PNF_SCTP_BACKLOG) < 0) {
        SM_Logs(LOG_CRTERR, _P5_, "[OAI_PNF] P5 listen() failed: %s",
                strerror(errno));
        close(fd);
        return -1;
    }
    p->listener_fd = fd;
    SM_Logs(LOG_INFO, _P5_, "[OAI_PNF] P5 SCTP listening on :%d.", port);
    return 0;
}

static int oai_pnf_open_p7_socket(oai_pnf_t* p)
{
    const xFAPI_Config* cfg = &p->ctx->config;
    int port = cfg->nfapi_socket.p7_local_port;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        SM_Logs(LOG_CRTERR, _P7_, "[OAI_PNF] P7 socket(UDP) failed: %s",
                strerror(errno));
        return -1;
    }
    int bufsz = 4 * 1024 * 1024;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        SM_Logs(LOG_CRTERR, _P7_, "[OAI_PNF] P7 bind(:%d) failed: %s",
                port, strerror(errno));
        close(fd);
        return -1;
    }
    p->p7_sock = fd;

    memset(&p->vnf_p7_addr, 0, sizeof(p->vnf_p7_addr));
    p->vnf_p7_addr.sin_family = AF_INET;
    p->vnf_p7_addr.sin_port   = htons((uint16_t)cfg->nfapi_socket.p7_remote_port);
    if (inet_pton(AF_INET, cfg->nfapi_socket.remote_ip,
                  &p->vnf_p7_addr.sin_addr) == 1) {
        p->p7_addr_known = 1;
    }
    return 0;
}

// ===========================================================================
// P5 listener thread: accept VNF, respond to handshake, survive disconnects
// ===========================================================================

static void* oai_pnf_p5_listener_thread(void* arg)
{
    oai_pnf_t* p = (oai_pnf_t*)arg;

    while (atomic_load(&p->running)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(p->listener_fd, &rfds);
        int maxfd = p->listener_fd;
        if (p->p5_sock >= 0) {
            FD_SET(p->p5_sock, &rfds);
            if (p->p5_sock > maxfd) maxfd = p->p5_sock;
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] P5 select() failed: %s",
                    strerror(errno));
            continue;
        }
        if (rc == 0) {
            continue;
        }
        if (FD_ISSET(p->listener_fd, &rfds)) {
            (void)oai_pnf_accept_vnf(p);
        }
        if (p->p5_sock >= 0 && FD_ISSET(p->p5_sock, &rfds)) {
            if (oai_pnf_p5_read_dispatch(p) <= 0) {
                SM_Logs(LOG_WARN, _P5_,
                        "[OAI_PNF] VNF P5 disconnected; tearing down session "
                        "and returning to listen.");
                oai_pnf_close_vnf_session(p);
            }
        }
    }
    return NULL;
}

static int oai_pnf_accept_vnf(oai_pnf_t* p)
{
    if (p->p5_sock >= 0) {
        struct sockaddr_in tmp;
        socklen_t tl = sizeof(tmp);
        int extra = accept(p->listener_fd, (struct sockaddr*)&tmp, &tl);
        if (extra >= 0) {
            SM_Logs(LOG_WARN, _P5_,
                    "[OAI_PNF] Rejecting extra VNF connection from %s:%d "
                    "(one VNF already attached).",
                    inet_ntoa(tmp.sin_addr), ntohs(tmp.sin_port));
            close(extra);
        }
        return -1;
    }
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int s = accept(p->listener_fd, (struct sockaddr*)&peer, &plen);
    if (s < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] P5 accept() failed: %s",
                strerror(errno));
        return -1;
    }
    p->p5_sock     = s;
    p->vnf_p5_addr = peer;
    p->state       = OAI_PNF_CONNECTED;
    SM_Logs(LOG_INFO, _P5_, "[OAI_PNF] VNF connected from %s:%d.",
            inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    return 0;
}

static void oai_pnf_close_vnf_session(oai_pnf_t* p)
{
    if (p->p5_sock >= 0) {
        close(p->p5_sock);
        p->p5_sock = -1;
    }
    p->state        = OAI_PNF_DISCONNECTED;
    p->p7_addr_known = 0;
}

// ===========================================================================
// P5 read + dispatch
// ===========================================================================

static int oai_pnf_p5_read_dispatch(oai_pnf_t* p)
{
    uint8_t hdr_buf[NFAPI_NR_P5_HEADER_LENGTH];
    struct sctp_sndrcvinfo sri;
    memset(&sri, 0, sizeof(sri));
    int flags = MSG_PEEK;
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    int n = sctp_recvmsg(p->p5_sock, hdr_buf, sizeof(hdr_buf),
                         (struct sockaddr*)&from, &fromlen, &sri, &flags);
    if (n <= 0) {
        if (n < 0) {
            SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] P5 sctp_recvmsg(peek) failed: %s",
                    strerror(errno));
        }
        return n;
    }
    if (n < (int)NFAPI_NR_P5_HEADER_LENGTH) {
        SM_Logs(LOG_WARN, _P5_,
                "[OAI_PNF] P5 short header peek (%d < %d); dropping.",
                n, (int)NFAPI_NR_P5_HEADER_LENGTH);
        return -1;
    }

    nfapi_nr_p4_p5_message_header_t header;
    memset(&header, 0, sizeof(header));
    if (!be_nfapi_nr_p5_message_header_unpack(hdr_buf, sizeof(hdr_buf),
                                              &header, sizeof(header),
                                              &p->p5_codec)) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_PNF] P5 NR header unpack failed; dropping.");
        return -1;
    }

    uint32_t total = (uint32_t)header.message_length + NFAPI_NR_P5_HEADER_LENGTH;
    if (total <= NFAPI_NR_P5_HEADER_LENGTH || total > OAI_PNF_TX_BUF_SIZE) {
        SM_Logs(LOG_ERROR, _P5_,
                "[OAI_PNF] P5 implausible message_length=%u (total=%u); dropping.",
                header.message_length, total);
        return -1;
    }

    uint8_t* buf = (uint8_t*)malloc(total);
    if (buf == NULL) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_PNF] P5 malloc(%u) failed; dropping.", total);
        return -1;
    }
    flags = 0;
    n = sctp_recvmsg(p->p5_sock, buf, total,
                     (struct sockaddr*)&from, &fromlen, &sri, &flags);
    if (n <= 0) {
        free(buf);
        if (n < 0) {
            SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] P5 sctp_recvmsg(full) failed: %s",
                    strerror(errno));
        }
        return n;
    }
    oai_pnf_handle_p5_message(p, buf, (uint32_t)n);
    free(buf);
    return 1;
}

// ===========================================================================
// P5 send (pack + sctp_sendmsg)
// ===========================================================================

static int oai_pnf_send_p5(oai_pnf_t* p, nfapi_nr_p4_p5_message_header_t* hdr,
                           uint32_t msg_len)
{
    if (p->p5_sock < 0) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_PNF] P5 send with no VNF connected.");
        return -1;
    }
    int packed = be_nfapi_nr_p5_message_pack(hdr, msg_len,
                                             p->p5_tx_buf, sizeof(p->p5_tx_buf),
                                             &p->p5_codec);
    if (packed < 0) {
        SM_Logs(LOG_ERROR, _P5_,
                "[OAI_PNF] p5_message_pack failed (rc=%d, msg_id=0x%04x).",
                packed, hdr->message_id);
        return -1;
    }
    int sent = sctp_sendmsg(p->p5_sock, p->p5_tx_buf, packed,
                            (struct sockaddr*)&p->vnf_p5_addr,
                            sizeof(p->vnf_p5_addr),
                            /*ppid*/0, /*flags*/0, /*stream*/0,
                            /*timetolive*/0, /*context*/0);
    if (sent != packed) {
        SM_Logs(LOG_ERROR, _P5_,
                "[OAI_PNF] sctp_sendmsg sent %d/%d (msg_id=0x%04x): %s",
                sent, packed, hdr->message_id, strerror(errno));
        return -1;
    }
    return 0;
}

// ===========================================================================
// Handshake responder
// ===========================================================================

static int oai_pnf_send_pnf_param_response(oai_pnf_t* p)
{
    nfapi_nr_pnf_param_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_PARAM_RESPONSE;
    resp.header.phy_id     = 0;
    resp.error_code        = NFAPI_MSG_OK;
    // Advertise a single PHY so the VNF's PNF_CONFIG.request can target it.
    resp.num_tlvs          = 1;
    resp.pnf_phy.tl.tag    = NFAPI_PNF_PHY_TAG;
    resp.pnf_phy.number_of_phys = 1;
    resp.pnf_phy.phy[0].phy_config_index = 0;
    return oai_pnf_send_p5(p, &resp.header, sizeof(resp));
}

static int oai_pnf_send_pnf_config_response(oai_pnf_t* p)
{
    nfapi_nr_pnf_config_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_CONFIG_RESPONSE;
    resp.header.phy_id     = 0;
    resp.error_code        = NFAPI_MSG_OK;
    return oai_pnf_send_p5(p, &resp.header, sizeof(resp));
}

static void oai_pnf_handle_p5_message(oai_pnf_t* p, uint8_t* buf, uint32_t len)
{
    nfapi_nr_p4_p5_message_header_t header;
    memset(&header, 0, sizeof(header));
    if (!be_nfapi_nr_p5_message_header_unpack(buf, len, &header, sizeof(header),
                                              &p->p5_codec)) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_PNF] P5 dispatch NR header unpack failed.");
        return;
    }

    switch (header.message_id) {
        case NFAPI_NR_PHY_MSG_TYPE_PNF_PARAM_REQUEST:
            SM_Logs(LOG_INFO, _P5_, "[OAI_PNF] <- PNF_PARAM.request; -> response.");
            if (oai_pnf_send_pnf_param_response(p) != 0) {
                oai_pnf_close_vnf_session(p);
            }
            break;

        case NFAPI_NR_PHY_MSG_TYPE_PNF_CONFIG_REQUEST:
            SM_Logs(LOG_INFO, _P5_, "[OAI_PNF] <- PNF_CONFIG.request; -> response.");
            if (oai_pnf_send_pnf_config_response(p) != 0) {
                oai_pnf_close_vnf_session(p);
            } else {
                p->state = OAI_PNF_CONFIGURED;
            }
            break;

        case NFAPI_NR_PHY_MSG_TYPE_PNF_START_REQUEST:
            // OAI's VNF goes RUNNING after sending PNF_START.request and does
            // not require a PNF_START.response; entering RUNNING here is enough.
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_PNF] <- PNF_START.request; PNF entering RUNNING.");
            p->state = OAI_PNF_RUNNING;
            break;

        // Cell-level P5 (PARAM/CONFIG/START.request) and their responses are
        // handled by the data-plane bridge; ignore until that lands.
        default:
            SM_Logs(LOG_WARN, _P5_,
                    "[OAI_PNF] <- Unhandled P5 msg_id=0x%04x (len=%u).",
                    header.message_id, len);
            break;
    }
}

// ===========================================================================
// Aerial -> OAI bridge (data plane lands with the re-frame step)
// ===========================================================================

int aerial_oai_from_aerial(struct AppContext* ctx, int32_t msg_id,
                           const uint8_t* scf_msg, uint32_t scf_len,
                           const uint8_t* data_buf, uint32_t data_len)
{
    (void)ctx; (void)scf_msg; (void)scf_len; (void)data_buf; (void)data_len;
    SM_Logs(LOG_DEBUG, _P7_,
            "[AERIAL_OAI Aerial->OAI] rx msg_id=0x%02x scf_len=%u data_len=%u "
            "(bridge pending).",
            msg_id, scf_len, data_len);
    return 0;
}

#endif /* AERIAL_OAI */
