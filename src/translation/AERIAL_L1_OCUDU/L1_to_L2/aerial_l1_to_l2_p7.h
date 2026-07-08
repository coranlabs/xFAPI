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

// AERIAL_OCUDU P7 translation, L1 -> L2: SCF FAPI indications (nvIPC)
// -> OCUDU-FAPI (xSM to OCUDU-L2).

#ifndef AERIAL_L1_OCUDU_L1_TO_L2_P7_H
#define AERIAL_L1_OCUDU_L1_TO_L2_P7_H

#ifdef AERIAL_OCUDU

#include <stdint.h>

#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"

struct AppContext;

// Fixed SCF prologue: scf_fapi_header_t (message_count u8, handle_id u8) +
// scf_fapi_body_header_t (type_id u16, length u32). The body starts here.
#define AERIAL_SCF_MSG_HDR_SIZE 8u

// Dispatcher: scf_msg/msg_len is the whole nv_ipc_msg_t.msg_buf as received;
// data_buf/data_len is the companion nv_ipc_msg_t.data_buf, which Aerial uses to
// carry RX_DATA transport blocks out of line. Unpacks the SCF wire into the
// matching nfapi_nr_*_t and hands it to the per-message translator.
// Returns 0 on success, -1 on error.
int aerial_p7_translate_to_l2(struct AppContext* ctx, int32_t msg_id,
                              const uint8_t* scf_msg, uint32_t msg_len,
                              const uint8_t* data_buf, uint32_t data_len);

int aerial_l1l2_slot_indication(struct AppContext* ctx,
                                const uint8_t* scf_msg, uint32_t msg_len);

int aerial_l1l2_rach_indication(struct AppContext* ctx,
                                const nfapi_nr_rach_indication_t* ind);

int aerial_l1l2_crc_indication(struct AppContext* ctx,
                               const nfapi_nr_crc_indication_t* ind);

int aerial_l1l2_uci_indication(struct AppContext* ctx,
                               const nfapi_nr_uci_indication_t* ind);

int aerial_l1l2_srs_indication(struct AppContext* ctx,
                               const nfapi_nr_srs_indication_t* ind);

// RX_DATA keeps its own reader: the codec unpack pulls transport blocks from
// msg_buf, but Aerial packs them into data_buf.
int aerial_l1l2_rx_data_indication(struct AppContext* ctx,
                                   const uint8_t* scf_msg, uint32_t msg_len,
                                   const uint8_t* data_buf, uint32_t data_len);

int aerial_p7_cell_mu(struct AppContext* ctx);

// ul_cqi (128 + 2*SNR_dB) -> OCUDU ul_sinr_metric, 0.002 dB steps.
static inline int16_t aerial_cqi_to_ul_sinr_metric(uint8_t ul_cqi)
{
    int32_t m = ((int32_t)ul_cqi - 128) * 250;
    if (m >  32767) m =  32767;
    if (m < -32767) m = -32767;
    return (int16_t)m;
}

#endif /* AERIAL_OCUDU */
#endif /* AERIAL_L1_OCUDU_L1_TO_L2_P7_H */
