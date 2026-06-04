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

// OAI_OCUDU VNF — P5 (SCTP) control plane + handshake state machine.
//
// xFAPI is the nFAPI VNF (server): it listens on the P5 SCTP port, accepts
// the OAI L1 (PNF) connection, and drives the PNF-level handshake
// (PNF_PARAM -> PNF_CONFIG -> PNF_START). On PNF_START.response it brings up
// the P7 data plane (see oai_p7.c) and issues the cell-level PARAM.request.
//
// Resilience: on PNF disconnect or any fatal handshake error, all P7 threads
// and sockets are torn down, state resets to DISCONNECTED, and the listener
// loops back to accept() a fresh PNF connection — the bridge survives OAI
// restarts without restarting xFAPI.
//
// This file is OUR code. It calls only the third-party codec's public
// pack/unpack API; the open-nFAPI sources are never edited.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "oai_vnf.h"

#ifdef OAI_OCUDU

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "../../main/app_context.h"
#include "oai_p7.h"
#include "unified_logger.h"

// Forward declarations (P5 internals).
static int  oai_vnf_open_p5_listener(oai_vnf_t* v);
static int  oai_vnf_open_p7_socket(oai_vnf_t* v);
static void oai_vnf_close_pnf_session(oai_vnf_t* v);
static void* oai_vnf_p5_listener_thread(void* arg);
static int  oai_vnf_accept_pnf(oai_vnf_t* v);
static int  oai_vnf_p5_read_dispatch(oai_vnf_t* v);
static int  oai_vnf_send_p5(oai_vnf_t* v, nfapi_nr_p4_p5_message_header_t* hdr, uint32_t msg_len);

// Handshake steps.
static int  oai_vnf_send_pnf_param_req(oai_vnf_t* v);
static int  oai_vnf_send_pnf_config_req(oai_vnf_t* v);
static int  oai_vnf_send_pnf_start_req(oai_vnf_t* v);
static int  oai_vnf_send_cell_param_req(oai_vnf_t* v);
static void oai_vnf_handle_p5_message(oai_vnf_t* v, uint8_t* buf, uint32_t len);

// ===========================================================================
// Lifecycle
// ===========================================================================

int oai_vnf_start(struct AppContext* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    oai_vnf_t* v = (oai_vnf_t*)calloc(1, sizeof(oai_vnf_t));
    if (v == NULL) {
        SM_Logs(LOG_CRTERR, _XFAPI_, "[OAI_VNF] calloc(oai_vnf_t) failed.");
        return -1;
    }
    v->ctx        = ctx;
    v->listener_fd = -1;
    v->p5_sock     = -1;
    v->p7_sock     = -1;
    v->phy_id      = 1;
    v->p7_sequence = 0;
    v->pnf_state   = OAI_VNF_PNF_DISCONNECTED;
    v->checksum_enabled = ctx->config.nfapi_socket.checksum_enabled ? 1 : 0;
    atomic_store(&v->running, 1);

    // Codec configs: zero-init => codec uses its default malloc/free and no
    // vendor-extension callbacks. Matches the proven backup usage.
    memset(&v->p5_codec, 0, sizeof(v->p5_codec));
    memset(&v->p7_codec, 0, sizeof(v->p7_codec));

    oai_p7_rx_pool_init(&v->rx_pool);
    oai_p7_seg_queue_init(&v->seg_queue);

    if (oai_vnf_open_p7_socket(v) != 0) {
        free(v);
        return -1;
    }
    if (oai_vnf_open_p5_listener(v) != 0) {
        close(v->p7_sock);
        free(v);
        return -1;
    }

    ctx->oai_ocudu_ctx.vnf = v;

    // Spawn the P5 listener thread.
    int core = ctx->config.oai_forwarder.recv_core_id;
    int prio = ctx->config.oai_forwarder.priority;
    int rc = pthread_create(&v->p5_listener_tid, NULL,
                            oai_vnf_p5_listener_thread, v);
    if (rc != 0) {
        SM_Logs(LOG_CRTERR, _XFAPI_,
                "[OAI_VNF] pthread_create(p5_listener) failed: %d", rc);
        close(v->listener_fd);
        close(v->p7_sock);
        ctx->oai_ocudu_ctx.vnf = NULL;
        free(v);
        return -1;
    }
    v->p5_listener_started = 1;
    (void)core; (void)prio;  // pinning applied below (best-effort)

    if (core >= 0) {
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
        if (pthread_setaffinity_np(v->p5_listener_tid, sizeof(set), &set) != 0) {
            SM_Logs(LOG_WARN, _XFAPI_,
                    "[OAI_VNF] P5 listener CPU pin to %d failed.", core);
        }
    }

    SM_Logs(LOG_INFO, _XFAPI_,
            "[OAI_VNF] started. P5 SCTP listening on :%d, P7 UDP bound on :%d. "
            "Waiting for OAI L1 (PNF) to connect.",
            ctx->config.nfapi_socket.p5_local_port,
            ctx->config.nfapi_socket.p7_local_port);
    return 0;
}

void oai_vnf_stop(struct AppContext* ctx)
{
    if (ctx == NULL || ctx->oai_ocudu_ctx.vnf == NULL) {
        return;
    }
    oai_vnf_t* v = ctx->oai_ocudu_ctx.vnf;

    atomic_store(&v->running, 0);

    if (v->p5_listener_started) {
        pthread_join(v->p5_listener_tid, NULL);
        v->p5_listener_started = 0;
    }

    // Tear down any active PNF session (also stops P7 threads).
    oai_vnf_close_pnf_session(v);

    if (v->listener_fd >= 0) { close(v->listener_fd); v->listener_fd = -1; }
    if (v->p7_sock >= 0)     { close(v->p7_sock);     v->p7_sock = -1; }

    oai_p7_seg_queue_destroy(&v->seg_queue);

    ctx->oai_ocudu_ctx.vnf = NULL;
    free(v);
    SM_Logs(LOG_INFO, _XFAPI_, "[OAI_VNF] stopped, resources released.");
}

// ===========================================================================
// P5 SCTP listener socket
// ===========================================================================

static int oai_vnf_open_p5_listener(oai_vnf_t* v)
{
    const xFAPI_Config* cfg = &v->ctx->config;
    int port = cfg->nfapi_socket.p5_local_port;

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (fd < 0) {
        SM_Logs(LOG_CRTERR, _P5_,
                "[OAI_VNF] P5 socket(SCTP) failed: %s. Is the sctp kernel "
                "module loaded and libsctp installed?", strerror(errno));
        return -1;
    }

    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sctp_initmsg initmsg;
    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams  = OAI_VNF_SCTP_STREAMS;
    initmsg.sinit_max_instreams = OAI_VNF_SCTP_STREAMS;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(initmsg)) < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_VNF] setsockopt(SCTP_INITMSG) failed: %s",
                strerror(errno));
    }

    struct sctp_event_subscribe events;
    memset(&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_EVENTS, &events, sizeof(events)) < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_VNF] setsockopt(SCTP_EVENTS) failed: %s",
                strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        SM_Logs(LOG_CRTERR, _P5_, "[OAI_VNF] P5 bind(:%d) failed: %s",
                port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, OAI_VNF_SCTP_BACKLOG) < 0) {
        SM_Logs(LOG_CRTERR, _P5_, "[OAI_VNF] P5 listen() failed: %s",
                strerror(errno));
        close(fd);
        return -1;
    }

    v->listener_fd = fd;
    SM_Logs(LOG_INFO, _P5_,
            "[OAI_VNF] P5 SCTP listener up on :%d (fd=%d, backlog=%d).",
            port, fd, OAI_VNF_SCTP_BACKLOG);
    return 0;
}

// ===========================================================================
// P7 UDP socket
// ===========================================================================

static int oai_vnf_open_p7_socket(oai_vnf_t* v)
{
    const xFAPI_Config* cfg = &v->ctx->config;
    int port = cfg->nfapi_socket.p7_local_port;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        SM_Logs(LOG_CRTERR, _P7_, "[OAI_VNF] P7 socket(UDP) failed: %s",
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
        SM_Logs(LOG_CRTERR, _P7_, "[OAI_VNF] P7 bind(:%d) failed: %s",
                port, strerror(errno));
        close(fd);
        return -1;
    }

    v->p7_sock = fd;

    // Default P7 destination from config; may be refined by PARAM.response.
    memset(&v->pnf_p7_addr, 0, sizeof(v->pnf_p7_addr));
    v->pnf_p7_addr.sin_family = AF_INET;
    v->pnf_p7_addr.sin_port   = htons((uint16_t)cfg->nfapi_socket.p7_remote_port);
    if (inet_pton(AF_INET, cfg->nfapi_socket.remote_ip,
                  &v->pnf_p7_addr.sin_addr) == 1) {
        v->p7_addr_known = 1;
    }

    SM_Logs(LOG_INFO, _P7_,
            "[OAI_VNF] P7 UDP socket bound on :%d (fd=%d), default dest %s:%d.",
            port, fd, cfg->nfapi_socket.remote_ip, cfg->nfapi_socket.p7_remote_port);
    return 0;
}

// ===========================================================================
// P5 listener thread: accept PNF, run handshake, read P5, survive disconnects
// ===========================================================================

static void* oai_vnf_p5_listener_thread(void* arg)
{
    oai_vnf_t* v = (oai_vnf_t*)arg;
    SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] P5 listener thread started.");

    while (atomic_load(&v->running)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(v->listener_fd, &rfds);
        int maxfd = v->listener_fd;
        if (v->p5_sock >= 0) {
            FD_SET(v->p5_sock, &rfds);
            if (v->p5_sock > maxfd) maxfd = v->p5_sock;
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            SM_Logs(LOG_WARN, _P5_, "[OAI_VNF] P5 select() failed: %s",
                    strerror(errno));
            continue;
        }
        if (rc == 0) {
            continue;  // timeout — re-check running flag
        }

        // New PNF connection.
        if (FD_ISSET(v->listener_fd, &rfds)) {
            if (oai_vnf_accept_pnf(v) == 0) {
                // Kick off the handshake immediately after accept.
                if (oai_vnf_send_pnf_param_req(v) != 0) {
                    SM_Logs(LOG_ERROR, _P5_,
                            "[OAI_VNF] Failed to send PNF_PARAM.request; "
                            "resetting session.");
                    oai_vnf_close_pnf_session(v);
                }
            }
        }

        // Incoming P5 data from the connected PNF.
        if (v->p5_sock >= 0 && FD_ISSET(v->p5_sock, &rfds)) {
            if (oai_vnf_p5_read_dispatch(v) <= 0) {
                SM_Logs(LOG_WARN, _P5_,
                        "[OAI_VNF] PNF P5 disconnected; tearing down session "
                        "and returning to listen.");
                oai_vnf_close_pnf_session(v);
            }
        }
    }

    SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] P5 listener thread exiting.");
    return NULL;
}

static int oai_vnf_accept_pnf(oai_vnf_t* v)
{
    if (v->p5_sock >= 0) {
        // Already have a PNF; reject additional connections (single-PNF bridge).
        struct sockaddr_in tmp;
        socklen_t tl = sizeof(tmp);
        int extra = accept(v->listener_fd, (struct sockaddr*)&tmp, &tl);
        if (extra >= 0) {
            SM_Logs(LOG_WARN, _P5_,
                    "[OAI_VNF] Rejecting extra PNF connection from %s:%d "
                    "(one PNF already attached).",
                    inet_ntoa(tmp.sin_addr), ntohs(tmp.sin_port));
            close(extra);
        }
        return -1;
    }

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int s = accept(v->listener_fd, (struct sockaddr*)&peer, &plen);
    if (s < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_VNF] P5 accept() failed: %s",
                strerror(errno));
        return -1;
    }

    v->p5_sock    = s;
    v->pnf_p5_addr = peer;
    v->pnf_state  = OAI_VNF_PNF_CONNECTED;

    SM_Logs(LOG_INFO, _P5_,
            "[OAI_VNF] PNF (OAI L1) connected from %s:%d (p5_sock=%d). "
            "Starting handshake.",
            inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), s);
    return 0;
}

// Tear down the current PNF session: stop P7 threads, close P5 socket, reset
// state. Leaves the listener_fd + p7_sock open so we can accept a fresh PNF.
static void oai_vnf_close_pnf_session(oai_vnf_t* v)
{
    if (v->p7_threads_started) {
        oai_p7_threads_stop(v);
        v->p7_threads_started = 0;
    }
    if (v->p5_sock >= 0) {
        close(v->p5_sock);
        v->p5_sock = -1;
    }
    oai_p7_seg_queue_reset(&v->seg_queue);
    v->pnf_state    = OAI_VNF_PNF_DISCONNECTED;
    v->p7_addr_known = 0;
    SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] PNF session reset -> DISCONNECTED.");
}

// ===========================================================================
// P5 read + dispatch
// ===========================================================================

// Returns >0 on a processed message, 0 on orderly disconnect, <0 on error.
static int oai_vnf_p5_read_dispatch(oai_vnf_t* v)
{
    // 1) Peek the NR P5 header (10 bytes: phy_id, message_id, message_length,
    //    spare) to learn the body length. message_length is the BODY length;
    //    the full wire message is NFAPI_NR_P5_HEADER_LENGTH + message_length.
    uint8_t hdr_buf[NFAPI_NR_P5_HEADER_LENGTH];
    struct sctp_sndrcvinfo sri;
    memset(&sri, 0, sizeof(sri));
    int flags = MSG_PEEK;
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    int n = sctp_recvmsg(v->p5_sock, hdr_buf, sizeof(hdr_buf),
                         (struct sockaddr*)&from, &fromlen, &sri, &flags);
    if (n <= 0) {
        if (n < 0) {
            SM_Logs(LOG_WARN, _P5_, "[OAI_VNF] P5 sctp_recvmsg(peek) failed: %s",
                    strerror(errno));
        }
        return n;  // 0 => disconnect, <0 => error
    }
    if (n < (int)NFAPI_NR_P5_HEADER_LENGTH) {
        SM_Logs(LOG_WARN, _P5_,
                "[OAI_VNF] P5 short header peek (%d < %d); dropping.",
                n, (int)NFAPI_NR_P5_HEADER_LENGTH);
        return -1;
    }

    nfapi_nr_p4_p5_message_header_t header;
    memset(&header, 0, sizeof(header));
    if (!nfapi_nr_p5_message_header_unpack(hdr_buf, sizeof(hdr_buf),
                                           &header, sizeof(header),
                                           &v->p5_codec)) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] P5 NR header unpack failed; dropping.");
        return -1;
    }

    uint32_t total = (uint32_t)header.message_length + NFAPI_NR_P5_HEADER_LENGTH;
    if (total <= NFAPI_NR_P5_HEADER_LENGTH || total > OAI_VNF_TX_BUF_SIZE) {
        SM_Logs(LOG_ERROR, _P5_,
                "[OAI_VNF] P5 implausible message_length=%u (total=%u); dropping.",
                header.message_length, total);
        return -1;
    }

    // 2) Read the full message.
    uint8_t* buf = (uint8_t*)malloc(total);
    if (buf == NULL) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] P5 malloc(%u) failed; dropping.", total);
        return -1;
    }
    flags = 0;
    n = sctp_recvmsg(v->p5_sock, buf, total,
                     (struct sockaddr*)&from, &fromlen, &sri, &flags);
    if (n <= 0) {
        free(buf);
        if (n < 0) {
            SM_Logs(LOG_WARN, _P5_, "[OAI_VNF] P5 sctp_recvmsg(full) failed: %s",
                    strerror(errno));
        }
        return n;
    }

    SM_Logs(LOG_DEBUG, _P5_,
            "[OAI_VNF] P5 RX msg_id=0x%04x len=%u (read %d bytes).",
            header.message_id, total, n);
    SM_Logs_Buffer(LOG_DEBUG, _P5_, "[OAI_VNF] P5 RX raw: ", buf, (uint32_t)n);

    oai_vnf_handle_p5_message(v, buf, (uint32_t)n);
    free(buf);
    return 1;
}

// ===========================================================================
// P5 message handler — the handshake state machine
// ===========================================================================

static void oai_vnf_handle_p5_message(oai_vnf_t* v, uint8_t* buf, uint32_t len)
{
    nfapi_nr_p4_p5_message_header_t header;
    memset(&header, 0, sizeof(header));
    if (!nfapi_nr_p5_message_header_unpack(buf, len, &header, sizeof(header),
                                           &v->p5_codec)) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] P5 dispatch NR header unpack failed.");
        return;
    }

    switch (header.message_id) {
        case NFAPI_NR_PHY_MSG_TYPE_PNF_PARAM_RESPONSE: {
            nfapi_nr_pnf_param_response_t resp;
            memset(&resp, 0, sizeof(resp));
            if (!nfapi_nr_p5_message_unpack(buf, len, &resp, sizeof(resp),
                                            &v->p5_codec)) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] PNF_PARAM.response unpack failed.");
                break;
            }
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_VNF] <- PNF_PARAM.response (error_code=%u).",
                    resp.error_code);
            if (resp.vendor_extension)
                v->p5_codec.deallocate(resp.vendor_extension);
            if (oai_vnf_send_pnf_config_req(v) != 0) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] Failed to send PNF_CONFIG.request.");
                oai_vnf_close_pnf_session(v);
            }
            break;
        }

        case NFAPI_NR_PHY_MSG_TYPE_PNF_CONFIG_RESPONSE: {
            nfapi_nr_pnf_config_response_t resp;
            memset(&resp, 0, sizeof(resp));
            if (!nfapi_nr_p5_message_unpack(buf, len, &resp, sizeof(resp),
                                            &v->p5_codec)) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] PNF_CONFIG.response unpack failed.");
                break;
            }
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_VNF] <- PNF_CONFIG.response (error_code=%u).",
                    resp.error_code);
            if (resp.vendor_extension)
                v->p5_codec.deallocate(resp.vendor_extension);
            if (oai_vnf_send_pnf_start_req(v) != 0) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] Failed to send PNF_START.request.");
                oai_vnf_close_pnf_session(v);
            }
            break;
        }

        case NFAPI_NR_PHY_MSG_TYPE_PNF_START_RESPONSE: {
            nfapi_nr_pnf_start_response_t resp;
            memset(&resp, 0, sizeof(resp));
            if (!nfapi_nr_p5_message_unpack(buf, len, &resp, sizeof(resp),
                                            &v->p5_codec)) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] PNF_START.response unpack failed.");
                break;
            }
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_VNF] <- PNF_START.response (error_code=%u). "
                    "PNF handshake complete; bringing up P7.",
                    resp.error_code);
            if (resp.vendor_extension)
                v->p5_codec.deallocate(resp.vendor_extension);

            v->pnf_state = OAI_VNF_PNF_RUNNING;

            // Bring up the P7 data plane (listener + rx_task).
            if (oai_p7_threads_start(v) != 0) {
                SM_Logs(LOG_CRTERR, _P7_,
                        "[OAI_VNF] Failed to start P7 threads; resetting session.");
                oai_vnf_close_pnf_session(v);
                break;
            }
            v->p7_threads_started = 1;

            // Issue the cell-level PARAM.request. Cell-level CONFIG/START are
            // driven by OCUDU L2 over xSM (see oai_l1_interface / forwarder).
            if (oai_vnf_send_cell_param_req(v) != 0) {
                SM_Logs(LOG_WARN, _P5_,
                        "[OAI_VNF] Failed to send cell PARAM.request.");
            }
            break;
        }

        case NFAPI_NR_PHY_MSG_TYPE_PARAM_RESPONSE: {
            nfapi_nr_param_response_scf_t resp;
            memset(&resp, 0, sizeof(resp));
            if (!nfapi_nr_p5_message_unpack(buf, len, &resp, sizeof(resp),
                                            &v->p5_codec)) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] PARAM.response unpack failed.");
                break;
            }
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_VNF] <- PARAM.response (error_code=%u).",
                    resp.error_code);

            // The PNF reports its P7 address/port here; latch it for P7 TX.
            if (resp.nfapi_config.p7_pnf_address_ipv4.tl.tag) {
                memcpy(&v->pnf_p7_addr.sin_addr.s_addr,
                       resp.nfapi_config.p7_pnf_address_ipv4.address,
                       NFAPI_IPV4_ADDRESS_LENGTH);
                SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] PNF P7 IPv4 = %s",
                        inet_ntoa(v->pnf_p7_addr.sin_addr));
            }
            if (resp.nfapi_config.p7_pnf_port.tl.tag) {
                v->pnf_p7_addr.sin_port =
                    htons(resp.nfapi_config.p7_pnf_port.value);
                v->p7_addr_known = 1;
                SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] PNF P7 port = %u",
                        resp.nfapi_config.p7_pnf_port.value);
            }
            if (resp.vendor_extension)
                v->p5_codec.deallocate(resp.vendor_extension);
            break;
        }

        case NFAPI_NR_PHY_MSG_TYPE_CONFIG_RESPONSE: {
            nfapi_nr_config_response_scf_t resp;
            memset(&resp, 0, sizeof(resp));
            if (!nfapi_nr_p5_message_unpack(buf, len, &resp, sizeof(resp),
                                            &v->p5_codec)) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] CONFIG.response unpack failed.");
                break;
            }
            SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] <- CONFIG.response (error_code=%u).",
                    resp.error_code);
            if (resp.vendor_extension)
                v->p5_codec.deallocate(resp.vendor_extension);
            // TODO(phase-translation): forward CONFIG.response toward OCUDU L2.
            break;
        }

        case NFAPI_NR_PHY_MSG_TYPE_START_RESPONSE: {
            nfapi_nr_start_response_scf_t resp;
            memset(&resp, 0, sizeof(resp));
            if (!nfapi_nr_p5_message_unpack(buf, len, &resp, sizeof(resp),
                                            &v->p5_codec)) {
                SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] START.response unpack failed.");
                break;
            }
            SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] <- START.response. Cell is RUNNING.");
            if (resp.vendor_extension)
                v->p5_codec.deallocate(resp.vendor_extension);
            // TODO(phase-translation): forward START.response toward OCUDU L2.
            break;
        }

        default:
            SM_Logs(LOG_WARN, _P5_,
                    "[OAI_VNF] <- Unhandled P5 msg_id=0x%04x (len=%u).",
                    header.message_id, len);
            break;
    }
}

// ===========================================================================
// P5 send (pack + sctp_sendmsg)
// ===========================================================================

static int oai_vnf_send_p5(oai_vnf_t* v, nfapi_nr_p4_p5_message_header_t* hdr,
                           uint32_t msg_len)
{
    if (v->p5_sock < 0) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_VNF] P5 send with no PNF connected.");
        return -1;
    }

    int packed = nfapi_nr_p5_message_pack(hdr, msg_len,
                                          v->p5_tx_buf, sizeof(v->p5_tx_buf),
                                          &v->p5_codec);
    if (packed < 0) {
        SM_Logs(LOG_ERROR, _P5_,
                "[OAI_VNF] nfapi_nr_p5_message_pack failed (rc=%d, msg_id=0x%04x).",
                packed, hdr->message_id);
        return -1;
    }

    SM_Logs_Buffer(LOG_DEBUG, _P5_, "[OAI_VNF] P5 TX raw: ",
                   v->p5_tx_buf, (uint32_t)packed);

    int sent = sctp_sendmsg(v->p5_sock, v->p5_tx_buf, packed,
                            (struct sockaddr*)&v->pnf_p5_addr,
                            sizeof(v->pnf_p5_addr),
                            /*ppid*/0, /*flags*/0, /*stream*/0,
                            /*timetolive*/0, /*context*/0);
    if (sent != packed) {
        SM_Logs(LOG_ERROR, _P5_,
                "[OAI_VNF] sctp_sendmsg sent %d/%d (msg_id=0x%04x): %s",
                sent, packed, hdr->message_id, strerror(errno));
        return -1;
    }
    return 0;
}

// ===========================================================================
// Handshake request builders
// ===========================================================================

static int oai_vnf_send_pnf_param_req(oai_vnf_t* v)
{
    nfapi_nr_pnf_param_request_t req;
    memset(&req, 0, sizeof(req));
    req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_PARAM_REQUEST;
    SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] -> PNF_PARAM.request");
    int rc = oai_vnf_send_p5(v, &req.header, sizeof(req));
    if (rc == 0) v->pnf_state = OAI_VNF_PNF_PNF_PARAM_SENT;
    return rc;
}

static int oai_vnf_send_pnf_config_req(oai_vnf_t* v)
{
    nfapi_nr_pnf_config_request_t req;
    memset(&req, 0, sizeof(req));
    req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_CONFIG_REQUEST;
    SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] -> PNF_CONFIG.request");
    int rc = oai_vnf_send_p5(v, &req.header, sizeof(req));
    if (rc == 0) v->pnf_state = OAI_VNF_PNF_PNF_CONFIG_SENT;
    return rc;
}

static int oai_vnf_send_pnf_start_req(oai_vnf_t* v)
{
    nfapi_nr_pnf_start_request_t req;
    memset(&req, 0, sizeof(req));
    req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_START_REQUEST;
    SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] -> PNF_START.request");
    int rc = oai_vnf_send_p5(v, &req.header, sizeof(req));
    if (rc == 0) v->pnf_state = OAI_VNF_PNF_PNF_START_SENT;
    return rc;
}

static int oai_vnf_send_cell_param_req(oai_vnf_t* v)
{
    nfapi_nr_param_request_scf_t req;
    memset(&req, 0, sizeof(req));
    req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PARAM_REQUEST;
    req.header.phy_id     = (uint16_t)v->phy_id;
    SM_Logs(LOG_INFO, _P5_, "[OAI_VNF] -> PARAM.request (cell, phy_id=%d)",
            v->phy_id);
    return oai_vnf_send_p5(v, &req.header, sizeof(req));
}

#endif /* OAI_OCUDU */
