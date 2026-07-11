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

// AERIAL_OAI re-frame bridge: unpack a message with one codec into the shared
// open-nFAPI struct, then pack the struct with the other codec. SCF (Aerial) is
// little-endian via fapi_nr_*; nFAPI (OAI) is big-endian via be_fapi_nr_* /
// be_nfapi_nr_*. The struct is the only common ground; no OCUDU representation.

#include "aerial_oai_bridge.h"

#ifdef AERIAL_OAI

#include <stdlib.h>
#include <string.h>

#include "../../main/app_context.h"
#include "aerial_send.h"
#include "oai_pnf.h"
#include "nfapi_codec_be.h"
#include "unified_logger.h"

#include "nfapi_interface.h"
#include "nfapi_nr_interface.h"
#include "nfapi_nr_interface_scf.h"
#include "nr_fapi.h"
#include "nr_fapi_p5.h"
#include "nr_fapi_p5_utils.h"
#include "nr_fapi_p7.h"
#include "nr_fapi_p7_utils.h"

// Largest P7 message struct (tx_data_request inlines its PDU/TLV arrays). The
// scratch union is heap-allocated once per bridge helper call site is too big
// for the stack, so P7 uses a thread-local reused buffer.
#define AERIAL_OAI_SCF_HDR_SIZE 8u

// ---------------------------------------------------------------------------
// P5 (small structs: stack union is safe).
// ---------------------------------------------------------------------------

typedef union {
    nfapi_nr_p4_p5_message_header_t header;
    nfapi_nr_param_request_scf_t    param_req;
    nfapi_nr_param_response_scf_t   param_resp;
    nfapi_nr_config_request_scf_t   config_req;
    nfapi_nr_config_response_scf_t  config_resp;
    nfapi_nr_start_request_scf_t    start_req;
    nfapi_nr_start_response_scf_t   start_resp;
    nfapi_nr_stop_request_scf_t     stop_req;
    nfapi_nr_stop_indication_scf_t  stop_ind;
} p5_msg_u;

// Free whatever the P5 unpack allocated inside the message struct (TLV lists,
// per-slot tables, vendor extension).
static void p5_free(uint16_t msg_id, p5_msg_u* u)
{
    switch (msg_id) {
        case NFAPI_NR_PHY_MSG_TYPE_PARAM_REQUEST:
            free_param_request(&u->param_req); break;
        case NFAPI_NR_PHY_MSG_TYPE_PARAM_RESPONSE:
            free_param_response(&u->param_resp); break;
        case NFAPI_NR_PHY_MSG_TYPE_CONFIG_REQUEST:
            free_config_request(&u->config_req); break;
        case NFAPI_NR_PHY_MSG_TYPE_CONFIG_RESPONSE:
            free_config_response(&u->config_resp); break;
        case NFAPI_NR_PHY_MSG_TYPE_START_REQUEST:
            free_start_request(&u->start_req); break;
        case NFAPI_NR_PHY_MSG_TYPE_START_RESPONSE:
            free_start_response(&u->start_resp); break;
        case NFAPI_NR_PHY_MSG_TYPE_STOP_REQUEST:
            free_stop_request(&u->stop_req); break;
        case NFAPI_NR_PHY_MSG_TYPE_STOP_INDICATION:
            free_stop_indication(&u->stop_ind); break;
        default: break;
    }
}

// OAI -> Aerial: big-endian nFAPI P5 request -> struct -> little-endian SCF ->
// Aerial over nvIPC.
int aerial_oai_bridge_p5_to_aerial(struct AppContext* ctx,
                                   const uint8_t* nfapi_msg, uint32_t len)
{
    if (ctx == NULL || nfapi_msg == NULL || len < NFAPI_NR_P5_HEADER_LENGTH) {
        return -1;
    }
    p5_msg_u u;
    memset(&u, 0, sizeof(u));
    if (!be_nfapi_nr_p5_message_unpack((void*)nfapi_msg, len, &u, sizeof(u), NULL)) {
        SM_Logs(LOG_ERROR, _P5_,
                "[AERIAL_OAI OAI->Aerial] P5 nFAPI unpack failed (len=%u).", len);
        p5_free(u.header.message_id, &u);
        return -1;
    }
    // aerial_send_p5_msg packs from the struct with the little-endian codec.
    int rc = aerial_send_p5_msg(ctx, &u.header, sizeof(u));
    p5_free(u.header.message_id, &u);
    return rc;
}

// Aerial -> OAI: little-endian SCF P5 response -> struct -> big-endian nFAPI ->
// OAI over SCTP.
int aerial_oai_bridge_p5_to_oai(struct AppContext* ctx, int32_t msg_id,
                                const uint8_t* scf_msg, uint32_t scf_len)
{
    (void)msg_id;
    if (ctx == NULL || scf_msg == NULL || scf_len < AERIAL_OAI_SCF_HDR_SIZE) {
        return -1;
    }
    p5_msg_u u;
    memset(&u, 0, sizeof(u));
    if (!fapi_nr_p5_message_unpack((void*)scf_msg, scf_len, &u, sizeof(u), NULL)) {
        SM_Logs(LOG_ERROR, _P5_,
                "[AERIAL_OAI Aerial->OAI] P5 SCF unpack failed (len=%u).", scf_len);
        p5_free(u.header.message_id, &u);
        return -1;
    }
    int rc = oai_pnf_send_p5(ctx, &u.header, sizeof(u));
    p5_free(u.header.message_id, &u);
    return rc;
}

// ---------------------------------------------------------------------------
// P7 (large structs: one heap scratch per direction, reused across calls).
// ---------------------------------------------------------------------------

// Free whatever the codec unpack allocated inside a P7 message struct.
static void p7_free(uint16_t msg_id, void* msg)
{
    switch (msg_id) {
        case NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST:
            free_dl_tti_request((nfapi_nr_dl_tti_request_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_UL_TTI_REQUEST:
            free_ul_tti_request((nfapi_nr_ul_tti_request_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_UL_DCI_REQUEST:
            free_ul_dci_request((nfapi_nr_ul_dci_request_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST:
            free_tx_data_request((nfapi_nr_tx_data_request_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION:
            free_rx_data_indication((nfapi_nr_rx_data_indication_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_CRC_INDICATION:
            free_crc_indication((nfapi_nr_crc_indication_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_UCI_INDICATION:
            free_uci_indication((nfapi_nr_uci_indication_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_SRS_INDICATION:
            free_srs_indication((nfapi_nr_srs_indication_t*)msg); break;
        case NFAPI_NR_PHY_MSG_TYPE_RACH_INDICATION:
            free_rach_indication((nfapi_nr_rach_indication_t*)msg); break;
        default: break;
    }
}

// Scratch big enough for the largest P7 struct (tx_data_request). Allocated
// lazily per thread; the bridge is driven by two threads (nvIPC RX for the
// downlink-from-Aerial P7 indications, and the P7 rx_task for uplink-from-OAI
// requests), so a __thread buffer avoids cross-thread contention without locks.
static __thread uint8_t* g_p7_scratch = NULL;

static void* p7_scratch(void)
{
    if (g_p7_scratch == NULL) {
        g_p7_scratch = (uint8_t*)malloc(sizeof(nfapi_nr_tx_data_request_t));
    }
    return g_p7_scratch;
}

// OAI -> Aerial: big-endian nFAPI P7 request -> struct -> little-endian SCF ->
// Aerial over nvIPC.
int aerial_oai_bridge_p7_to_aerial(struct AppContext* ctx,
                                   const uint8_t* nfapi_msg, uint32_t len)
{
    if (ctx == NULL || nfapi_msg == NULL || len < NFAPI_NR_P7_HEADER_LENGTH) {
        return -1;
    }
    void* scratch = p7_scratch();
    if (scratch == NULL) {
        SM_Logs(LOG_ERROR, _P7_, "[AERIAL_OAI OAI->Aerial] P7 scratch alloc failed.");
        return -1;
    }
    memset(scratch, 0, sizeof(nfapi_nr_tx_data_request_t));

    if (!be_nfapi_nr_p7_message_unpack((void*)nfapi_msg, len,
                                       scratch, sizeof(nfapi_nr_tx_data_request_t),
                                       NULL)) {
        SM_Logs(LOG_ERROR, _P7_,
                "[AERIAL_OAI OAI->Aerial] P7 nFAPI unpack failed (len=%u).", len);
        p7_free(((nfapi_nr_p7_message_header_t*)scratch)->message_id, scratch);
        return -1;
    }
    nfapi_nr_p7_message_header_t* hdr = (nfapi_nr_p7_message_header_t*)scratch;
    int rc = aerial_send_p7_msg(ctx, hdr);
    p7_free(hdr->message_id, scratch);
    return rc;
}

// Little-endian cursor over the SCF body (bounds-checked).
typedef struct { const uint8_t* d; uint32_t len; uint32_t off; int err; } le_rd_t;
static uint8_t  le_u8(le_rd_t* r)  { if (r->err || r->off + 1u > r->len) { r->err = 1; return 0; }
                                     return r->d[r->off++]; }
static uint16_t le_u16(le_rd_t* r) { if (r->err || r->off + 2u > r->len) { r->err = 1; return 0; }
                                     uint16_t v = (uint16_t)(r->d[r->off] | ((uint16_t)r->d[r->off+1] << 8));
                                     r->off += 2; return v; }
static uint32_t le_u32(le_rd_t* r) { if (r->err || r->off + 4u > r->len) { r->err = 1; return 0; }
                                     uint32_t v = (uint32_t)r->d[r->off] | ((uint32_t)r->d[r->off+1] << 8)
                                        | ((uint32_t)r->d[r->off+2] << 16) | ((uint32_t)r->d[r->off+3] << 24);
                                     r->off += 4; return v; }

#define AERIAL_OAI_RXDATA_MAX_TB_BYTES 65536u

// RX_DATA needs a bespoke path: Aerial keeps the per-PDU headers in the SCF
// msg_buf and concatenates the transport blocks in the nvIPC data_buf, in PDU
// order. The generic SCF unpack instead reads the TB inline from msg_buf, which
// is not Aerial's layout — so the headers are parsed by hand here and each
// pdu->pdu is pointed at data_buf. Those pointers alias the caller's buffer and
// must NOT be freed; only the pdu_list allocation is owned here.
static int bridge_rx_data_to_oai(struct AppContext* ctx,
                                 const uint8_t* scf_msg, uint32_t scf_len,
                                 const uint8_t* data_buf, uint32_t data_len)
{
    le_rd_t r = { scf_msg + AERIAL_OAI_SCF_HDR_SIZE,
                  scf_len - AERIAL_OAI_SCF_HDR_SIZE, 0, 0 };
    uint16_t sfn   = le_u16(&r);
    uint16_t slot  = le_u16(&r);
    uint16_t n_pdu = le_u16(&r);
    if (r.err) {
        SM_Logs(LOG_ERROR, _P7_, "[AERIAL_OAI Aerial->OAI] RX_DATA header short.");
        return -1;
    }

    nfapi_nr_rx_data_indication_t ind;
    memset(&ind, 0, sizeof(ind));
    ind.header.message_id = NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION;
    ind.sfn = sfn;
    ind.slot = slot;
    ind.number_of_pdus = n_pdu;
    if (n_pdu > 0) {
        ind.pdu_list = (nfapi_nr_rx_data_pdu_t*)calloc(n_pdu, sizeof(*ind.pdu_list));
        if (ind.pdu_list == NULL) {
            SM_Logs(LOG_ERROR, _P7_, "[AERIAL_OAI Aerial->OAI] RX_DATA calloc failed.");
            return -1;
        }
    }

    uint32_t tb_off = 0;
    for (uint16_t i = 0; i < n_pdu && !r.err; ++i) {
        nfapi_nr_rx_data_pdu_t* pdu = &ind.pdu_list[i];
        pdu->handle         = le_u32(&r);
        pdu->rnti           = le_u16(&r);
        pdu->harq_id        = le_u8(&r);
        pdu->pdu_length     = le_u32(&r);
        pdu->ul_cqi         = le_u8(&r);
        pdu->timing_advance = le_u16(&r);
        pdu->rssi           = le_u16(&r);
        if (r.err) break;

        if (pdu->pdu_length > AERIAL_OAI_RXDATA_MAX_TB_BYTES ||
            data_buf == NULL || (uint64_t)tb_off + pdu->pdu_length > data_len) {
            SM_Logs(LOG_WARN, _P7_,
                    "[AERIAL_OAI Aerial->OAI] RX_DATA pdu %u len=%u overruns "
                    "data_buf (%u); dropping.", i, pdu->pdu_length, data_len);
            r.err = 1;
            break;
        }
        pdu->pdu = (uint8_t*)(data_buf + tb_off);  // aliases data_buf; not freed
        tb_off  += pdu->pdu_length;
    }

    int rc = r.err ? -1 : oai_pnf_send_p7(ctx, &ind.header);
    // Free only the list; pdu->pdu entries alias the caller's data_buf.
    free(ind.pdu_list);
    return rc;
}

// Aerial -> OAI: little-endian SCF P7 indication -> struct -> big-endian nFAPI ->
// OAI over UDP.
int aerial_oai_bridge_p7_to_oai(struct AppContext* ctx, int32_t msg_id,
                                const uint8_t* scf_msg, uint32_t scf_len,
                                const uint8_t* data_buf, uint32_t data_len)
{
    if (ctx == NULL || scf_msg == NULL || scf_len < AERIAL_OAI_SCF_HDR_SIZE) {
        return -1;
    }
    if (msg_id == NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION) {
        return bridge_rx_data_to_oai(ctx, scf_msg, scf_len, data_buf, data_len);
    }

    void* scratch = p7_scratch();
    if (scratch == NULL) {
        SM_Logs(LOG_ERROR, _P7_, "[AERIAL_OAI Aerial->OAI] P7 scratch alloc failed.");
        return -1;
    }
    memset(scratch, 0, sizeof(nfapi_nr_tx_data_request_t));

    if (!fapi_nr_p7_message_unpack((void*)scf_msg, scf_len, scratch,
                                   sizeof(nfapi_nr_tx_data_request_t), NULL)) {
        SM_Logs(LOG_ERROR, _P7_,
                "[AERIAL_OAI Aerial->OAI] P7 SCF unpack failed (msg_id=0x%02x len=%u).",
                msg_id, scf_len);
        p7_free((uint16_t)msg_id, scratch);
        return -1;
    }
    int rc = oai_pnf_send_p7(ctx, (nfapi_nr_p7_message_header_t*)scratch);
    p7_free((uint16_t)msg_id, scratch);
    return rc;
}

#endif /* AERIAL_OAI */
