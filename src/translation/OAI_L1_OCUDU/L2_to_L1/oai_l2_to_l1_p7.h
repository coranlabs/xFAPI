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

// OAI_OCUDU P7 L2->L1: translate OCUDU-FAPI data-plane messages (DL_TTI / UL_TTI /
// UL_DCI / TX_DATA) into nFAPI and send them to the OAI L1 (PNF) over P7 UDP.

#ifndef OAI_L1_OCUDU_OAI_L2_TO_L1_P7_H
#define OAI_L1_OCUDU_OAI_L2_TO_L1_P7_H

#ifdef OAI_OCUDU

#include <stdint.h>

struct AppContext;
struct oai_vnf;

// Dispatch one OCUDU L2->L1 P7 message (src_va/size = full xSM buffer) to its nFAPI
// translator. Returns 1 if translated+sent, 0 if intentionally dropped, -1 on error.
int ocudu_p7_translate_and_send(struct AppContext* ctx, struct oai_vnf* v,
                                uint16_t type_id, const void* src_va,
                                uint32_t size);

// DL_TTI.request translator. body points past the 40-byte OCUDU xSM header;
// body_len is the declared body length. Same return convention as above.
int ocudu_l2l1_dl_tti_request(struct AppContext* ctx, struct oai_vnf* v,
                              const uint8_t* body, uint32_t body_len);

// TX_DATA.request translator (PDSCH transport blocks -> nFAPI TX_DATA).
int ocudu_l2l1_tx_data_request(struct AppContext* ctx, struct oai_vnf* v,
                               const uint8_t* body, uint32_t body_len);

// UL_TTI.request translator (PRACH -> nFAPI UL_TTI; arms PRACH detection).
int ocudu_l2l1_ul_tti_request(struct AppContext* ctx, struct oai_vnf* v,
                              const uint8_t* body, uint32_t body_len);

// UL_DCI.request translator (PDCCH UL-grant DCI 0_0/0_1 -> nFAPI UL_DCI).
// Required for connected-mode uplink grants (RRC Setup Complete, RLC STATUS, NAS).
int ocudu_l2l1_ul_dci_request(struct AppContext* ctx, struct oai_vnf* v,
                              const uint8_t* body, uint32_t body_len);

#endif /* OAI_OCUDU */
#endif /* OAI_L1_OCUDU_OAI_L2_TO_L1_P7_H */
