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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "oai_pnf.h"

#ifdef AERIAL_OAI

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "../../main/app_context.h"
#include "aerial_oai_bridge.h"
#include "nfapi_codec_be.h"
#include "unified_logger.h"

#include "nfapi_interface.h"
#include "nfapi_nr_interface.h"
#include "nfapi_nr_interface_scf.h"

static int  oai_pnf_connect_vnf(oai_pnf_t* p);
static int  oai_pnf_open_p7_socket(oai_pnf_t* p);
static void oai_pnf_close_vnf_session(oai_pnf_t* p);
static void* oai_pnf_p5_listener_thread(void* arg);
static int  oai_pnf_p5_read_dispatch(oai_pnf_t* p);
static void oai_pnf_handle_p5_message(oai_pnf_t* p, uint8_t* buf, uint32_t len);
static int  oai_pnf_send_p5_raw(oai_pnf_t* p, nfapi_nr_p4_p5_message_header_t* hdr,
                                uint32_t msg_len);
static void oai_pnf_enter_running(oai_pnf_t* p);

// Lifecycle

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
    p->p7_sequence = 0;
    p->state       = OAI_PNF_DISCONNECTED;
    p->checksum_enabled = ctx->config.nfapi_socket.checksum_enabled ? 1 : 0;
    atomic_store(&p->running, 1);

    memset(&p->p5_codec, 0, sizeof(p->p5_codec));
    memset(&p->p7_codec, 0, sizeof(p->p7_codec));

    oai_pnf_rx_pool_init(&p->rx_pool);
    oai_pnf_seg_queue_init(&p->seg_queue);

    if (oai_pnf_open_p7_socket(p) != 0) {
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

    oai_pnf_seg_queue_destroy(&p->seg_queue);

    ctx->aerial_oai_ctx.pnf = NULL;
    free(p);
}

// The OAI VNF is the SCTP server; xFAPI (PNF) connects out to it.
static int oai_pnf_connect_vnf(oai_pnf_t* p)
{
    const xFAPI_Config* cfg = &p->ctx->config;
    int rport = cfg->nfapi_socket.p5_remote_port;
    int lport = cfg->nfapi_socket.p5_local_port;

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (fd < 0) {
        SM_Logs(LOG_CRTERR, _P5_,
                "[OAI_PNF] P5 socket(SCTP) failed: %s. Is the sctp kernel "
                "module loaded and libsctp installed?", strerror(errno));
        return -1;
    }
    // No local bind: the reference PNF lets the kernel pick the source port.
    (void)lport;

    struct sctp_initmsg initmsg;
    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams  = OAI_PNF_SCTP_STREAMS;
    initmsg.sinit_max_instreams = OAI_PNF_SCTP_STREAMS;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(initmsg)) < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] setsockopt(SCTP_INITMSG) failed: %s",
                strerror(errno));
    }
    int no_delay = 1;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_NODELAY, &no_delay, sizeof(no_delay)) < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] setsockopt(SCTP_NODELAY) failed: %s",
                strerror(errno));
    }
    struct sctp_event_subscribe events;
    memset(&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;
    if (setsockopt(fd, SOL_SCTP, SCTP_EVENTS, &events, sizeof(events)) < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] setsockopt(SCTP_EVENTS) failed: %s",
                strerror(errno));
    }

    struct sockaddr_in raddr;
    memset(&raddr, 0, sizeof(raddr));
    raddr.sin_family = AF_INET;
    raddr.sin_port   = htons((uint16_t)rport);
    if (inet_pton(AF_INET, cfg->nfapi_socket.remote_ip, &raddr.sin_addr) != 1) {
        SM_Logs(LOG_CRTERR, _P5_, "[OAI_PNF] P5 bad remote_ip '%s'.",
                cfg->nfapi_socket.remote_ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
        close(fd);
        return -1;
    }

    p->p5_sock     = fd;
    p->vnf_p5_addr = raddr;
    p->state       = OAI_PNF_CONNECTED;
    SM_Logs(LOG_INFO, _P5_, "[OAI_PNF] P5 SCTP connected to VNF %s:%d.",
            cfg->nfapi_socket.remote_ip, rport);
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

// P5 listener thread

static void* oai_pnf_p5_listener_thread(void* arg)
{
    oai_pnf_t* p = (oai_pnf_t*)arg;

    while (atomic_load(&p->running)) {
        if (p->p5_sock < 0) {
            if (oai_pnf_connect_vnf(p) != 0) {
                struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
                nanosleep(&ts, NULL);
                continue;
            }
            if (!p->p7_addr_known) {
                p->vnf_p7_addr.sin_addr = p->vnf_p5_addr.sin_addr;
                p->p7_addr_known = 1;
            }
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(p->p5_sock, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int rc = select(p->p5_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] P5 select() failed: %s",
                    strerror(errno));
            continue;
        }
        if (rc == 0) {
            continue;
        }
        if (FD_ISSET(p->p5_sock, &rfds)) {
            if (oai_pnf_p5_read_dispatch(p) <= 0) {
                SM_Logs(LOG_WARN, _P5_,
                        "[OAI_PNF] VNF P5 disconnected; will reconnect.");
                oai_pnf_close_vnf_session(p);
            }
        }
    }
    return NULL;
}

static void oai_pnf_close_vnf_session(oai_pnf_t* p)
{
    if (p->p7_threads_started) {
        oai_pnf_p7_threads_stop(p);
        p->p7_threads_started = 0;
    }
    if (p->p5_sock >= 0) {
        close(p->p5_sock);
        p->p5_sock = -1;
    }
    oai_pnf_seg_queue_reset(&p->seg_queue);
    p->state        = OAI_PNF_DISCONNECTED;
    p->p7_addr_known = 0;
}

// Read one P5 event. Returns >0 to stay connected (message or a skipped SCTP
// notification), 0 on peer close, <0 on socket error. SCTP notifications are
// consumed, not decoded as FAPI.
static int oai_pnf_p5_read_dispatch(oai_pnf_t* p)
{
    uint8_t hdr_buf[NFAPI_NR_P5_HEADER_LENGTH];
    struct sctp_sndrcvinfo sri;
    memset(&sri, 0, sizeof(sri));
    int flags = MSG_PEEK;

    int n = sctp_recvmsg(p->p5_sock, hdr_buf, sizeof(hdr_buf),
                         NULL, NULL, &sri, &flags);
    SM_Logs(LOG_DEBUG, _P5_,
            "[OAI_PNF] P5 peek: n=%d errno=%d flags=0x%x", n,
            (n < 0) ? errno : 0, flags);
    if (n == 0) {
        return 0;                        // peer closed
    }
    if (n < 0) {
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] P5 peek failed: %s", strerror(errno));
        return -1;
    }
    if (n < (int)NFAPI_NR_P5_HEADER_LENGTH) {
        // A notification or a not-yet-complete datagram peeked short. Drain it
        // (below) so the socket advances; do NOT drop the association.
        return 1;
    }

    nfapi_nr_p4_p5_message_header_t header;
    memset(&header, 0, sizeof(header));
    uint32_t total = 0;
    int header_ok = be_nfapi_nr_p5_message_header_unpack(
        hdr_buf, sizeof(hdr_buf), &header, sizeof(header), &p->p5_codec);
    if (header_ok) {
        total = (uint32_t)header.message_length + NFAPI_NR_P5_HEADER_LENGTH;
    }
    // Size the read to the message when the header is sane, else to a
    // notification-sized scratch so the datagram is fully drained.
    uint32_t read_len = (header_ok && total > NFAPI_NR_P5_HEADER_LENGTH &&
                         total <= OAI_PNF_TX_BUF_SIZE)
                            ? total
                            : 512u;
    if (read_len < 512u) read_len = 512u;

    uint8_t* buf = (uint8_t*)malloc(read_len);
    if (buf == NULL) {
        SM_Logs(LOG_ERROR, _P5_, "[OAI_PNF] P5 malloc(%u) failed.", read_len);
        return 1;
    }

    flags = 0;
    n = sctp_recvmsg(p->p5_sock, buf, read_len, NULL, NULL, &sri, &flags);
    if (n == 0) {
        free(buf);
        return 0;
    }
    if (n < 0) {
        free(buf);
        SM_Logs(LOG_WARN, _P5_, "[OAI_PNF] P5 read failed: %s", strerror(errno));
        return -1;
    }

    if (flags & MSG_NOTIFICATION) {
        free(buf);
        return 1;
    }
    // OAI packs each P5 message into one SCTP datagram, so MSG_EOR is not gated.
    if (n >= (int)NFAPI_NR_P5_HEADER_LENGTH) {
        SM_Logs(LOG_DEBUG, _P5_,
                "[OAI_PNF] P5 rx %d bytes (msg_id=0x%04x, flags=0x%x).",
                n, header.message_id, flags);
        oai_pnf_handle_p5_message(p, buf, (uint32_t)n);
    }
    free(buf);
    return 1;
}

// P5 send (pack + sctp_sendmsg)

static int oai_pnf_send_p5_raw(oai_pnf_t* p, nfapi_nr_p4_p5_message_header_t* hdr,
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
    SM_Logs(LOG_DEBUG, _P5_,
            "[OAI_PNF] P5 tx %d bytes (msg_id=0x%04x) OK.", sent, hdr->message_id);
    return 0;
}

int oai_pnf_send_p5(struct AppContext* ctx,
                    nfapi_nr_p4_p5_message_header_t* hdr, uint32_t msg_len)
{
    if (ctx == NULL || ctx->aerial_oai_ctx.pnf == NULL || hdr == NULL) {
        return -1;
    }
    return oai_pnf_send_p5_raw(ctx->aerial_oai_ctx.pnf, hdr, msg_len);
}

// Handshake responder

static int oai_pnf_send_pnf_param_response(oai_pnf_t* p)
{
    nfapi_nr_pnf_param_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_PARAM_RESPONSE;
    resp.header.phy_id     = 0;
    resp.error_code        = NFAPI_MSG_OK;
    resp.num_tlvs          = 1;
    resp.pnf_phy.tl.tag    = NFAPI_PNF_PHY_TAG;
    resp.pnf_phy.number_of_phys = 1;
    resp.pnf_phy.phy[0].phy_config_index = 0;
    return oai_pnf_send_p5_raw(p, &resp.header, sizeof(resp));
}

static int oai_pnf_send_pnf_config_response(oai_pnf_t* p)
{
    nfapi_nr_pnf_config_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_CONFIG_RESPONSE;
    resp.header.phy_id     = 0;
    resp.error_code        = NFAPI_MSG_OK;
    return oai_pnf_send_p5_raw(p, &resp.header, sizeof(resp));
}

// The VNF waits for PNF_START.response before the cell PARAM.request.
static int oai_pnf_send_pnf_start_response(oai_pnf_t* p)
{
    nfapi_nr_pnf_start_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PNF_START_RESPONSE;
    resp.header.phy_id     = 0;
    resp.error_code        = NFAPI_MSG_OK;
    return oai_pnf_send_p5_raw(p, &resp.header, sizeof(resp));
}

// Aerial sends no cell START.response (it just starts SLOT.indication), but the
// VNF needs one (0x0108) to bring up its P7 connection, so xFAPI answers locally.
static int oai_pnf_send_start_response(oai_pnf_t* p)
{
    nfapi_nr_start_response_scf_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.message_id = NFAPI_NR_PHY_MSG_TYPE_START_RESPONSE;
    resp.header.phy_id     = 0;
    resp.error_code        = NFAPI_NR_START_MSG_OK;
    return oai_pnf_send_p5_raw(p, &resp.header, sizeof(resp));
}

// Aerial doesn't implement cell PARAM.request, so xFAPI answers it locally,
// advertising its own P7 endpoint (local_ip:p7_local_port) as the VNF's P7 dest.
static int oai_pnf_send_param_response(oai_pnf_t* p)
{
    const xFAPI_Config* cfg = &p->ctx->config;

    nfapi_nr_param_response_scf_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PARAM_RESPONSE;
    resp.header.phy_id     = 0;
    resp.error_code        = NFAPI_NR_PARAM_MSG_OK;

    struct in_addr p7_addr;
    memset(&p7_addr, 0, sizeof(p7_addr));
    if (cfg->nfapi_socket.local_ip[0] != '\0') {
        (void)inet_pton(AF_INET, cfg->nfapi_socket.local_ip, &p7_addr);
    }
    resp.nfapi_config.p7_pnf_address_ipv4.tl.tag =
        NFAPI_NR_NFAPI_P7_PNF_ADDRESS_IPV4_TAG;
    memcpy(resp.nfapi_config.p7_pnf_address_ipv4.address,
           &p7_addr.s_addr, NFAPI_IPV4_ADDRESS_LENGTH);
    resp.num_tlv++;

    // Host-order port (not htons): the VNF applies its own htons on the decoded value.
    resp.nfapi_config.p7_pnf_port.tl.tag = NFAPI_NR_NFAPI_P7_PNF_PORT_TAG;
    resp.nfapi_config.p7_pnf_port.value  =
        (uint16_t)cfg->nfapi_socket.p7_local_port;
    resp.num_tlv++;

    return oai_pnf_send_p5_raw(p, &resp.header, sizeof(resp));
}

static void oai_pnf_enter_running(oai_pnf_t* p)
{
    if (p->state == OAI_PNF_RUNNING) {
        return;
    }
    if (!p->p7_threads_started) {
        if (oai_pnf_p7_threads_start(p) != 0) {
            SM_Logs(LOG_ERROR, _P7_, "[OAI_PNF] P7 threads start failed.");
            return;
        }
        p->p7_threads_started = 1;
    }
    p->state = OAI_PNF_RUNNING;
    SM_Logs(LOG_INFO, _P5_, "[OAI_PNF] PNF RUNNING; P7 data plane active.");
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
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_PNF] <- PNF_START.request; -> response, PNF RUNNING.");
            oai_pnf_enter_running(p);
            if (oai_pnf_send_pnf_start_response(p) != 0) {
                oai_pnf_close_vnf_session(p);
            }
            break;

        case NFAPI_NR_PHY_MSG_TYPE_PARAM_REQUEST:
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_PNF] <- cell PARAM.request; -> response.");
            if (oai_pnf_send_param_response(p) != 0) {
                oai_pnf_close_vnf_session(p);
            }
            break;

        case NFAPI_NR_PHY_MSG_TYPE_CONFIG_REQUEST:
        case NFAPI_NR_PHY_MSG_TYPE_STOP_REQUEST:
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_PNF] <- cell P5 msg_id=0x%02x -> Aerial.",
                    header.message_id);
            (void)aerial_oai_bridge_p5_to_aerial(p->ctx, buf, len);
            break;

        case NFAPI_NR_PHY_MSG_TYPE_START_REQUEST:
            SM_Logs(LOG_INFO, _P5_,
                    "[OAI_PNF] <- cell START.request -> Aerial; -> local START.response.");
            (void)aerial_oai_bridge_p5_to_aerial(p->ctx, buf, len);
            if (oai_pnf_send_start_response(p) != 0) {
                oai_pnf_close_vnf_session(p);
            }
            break;

        default:
            SM_Logs(LOG_WARN, _P5_,
                    "[OAI_PNF] <- Unhandled P5 msg_id=0x%04x (len=%u).",
                    header.message_id, len);
            break;
    }
}

// Aerial -> OAI entry point (called by the nvIPC RX thread)

int aerial_oai_from_aerial(struct AppContext* ctx, int32_t msg_id,
                           const uint8_t* scf_msg, uint32_t scf_len,
                           const uint8_t* data_buf, uint32_t data_len)
{
    if (ctx == NULL || scf_msg == NULL) {
        return -1;
    }
    switch (msg_id) {
        case NFAPI_NR_PHY_MSG_TYPE_PARAM_RESPONSE:
        case NFAPI_NR_PHY_MSG_TYPE_CONFIG_RESPONSE:
        case NFAPI_NR_PHY_MSG_TYPE_START_RESPONSE:
        case NFAPI_NR_PHY_MSG_TYPE_STOP_INDICATION:
            return aerial_oai_bridge_p5_to_oai(ctx, msg_id, scf_msg, scf_len);

        case NFAPI_NR_PHY_MSG_TYPE_SLOT_INDICATION:
        case NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION:
        case NFAPI_NR_PHY_MSG_TYPE_CRC_INDICATION:
        case NFAPI_NR_PHY_MSG_TYPE_UCI_INDICATION:
        case NFAPI_NR_PHY_MSG_TYPE_SRS_INDICATION:
        case NFAPI_NR_PHY_MSG_TYPE_RACH_INDICATION:
            return aerial_oai_bridge_p7_to_oai(ctx, msg_id, scf_msg, scf_len,
                                               data_buf, data_len);

        case NFAPI_NR_PHY_MSG_TYPE_ERROR_INDICATION: {
            const uint8_t* b = scf_msg + 8u;
            if (scf_len >= 8u + 6u) {
                uint16_t sfn, slot;
                memcpy(&sfn, b, 2);
                memcpy(&slot, b + 2, 2);
                SM_Logs(LOG_ERROR, _P7_,
                        "[AERIAL_OAI Aerial->OAI] Aerial ERROR.indication SFN %u.%u "
                        "msg_id=0x%02x err_code=0x%02x.",
                        sfn, slot, b[4], b[5]);
            }
            return 0;
        }

        default:
            SM_Logs(LOG_DEBUG, _P7_,
                    "[AERIAL_OAI Aerial->OAI] rx msg_id=0x%02x scf_len=%u "
                    "(no route).", msg_id, scf_len);
            return 0;
    }
}

#endif /* AERIAL_OAI */
