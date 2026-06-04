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

// OAI_OCUDU VNF — P7 (UDP) data plane: receive path (listener + rx_task +
// segmentation reassembly), and send path (pack + segment + sendto).
//
// OUR code. Calls only the third-party codec's public API.

#ifndef OAI_OCUDU_OAI_P7_H
#define OAI_OCUDU_OAI_P7_H

#ifdef OAI_OCUDU

#include "oai_vnf.h"

// ---- RX buffer pool (preallocated, no per-datagram malloc on hot path) ----
void              oai_p7_rx_pool_init(oai_p7_rx_pool_t* pool);
oai_p7_rx_slot_t* oai_p7_rx_pool_acquire(oai_p7_rx_pool_t* pool);
void              oai_p7_rx_pool_release(oai_p7_rx_pool_t* pool,
                                         oai_p7_rx_slot_t* slot);

// ---- Segmentation reassembly queue ----
void oai_p7_seg_queue_init(oai_p7_seg_queue_t* q);
void oai_p7_seg_queue_reset(oai_p7_seg_queue_t* q);
void oai_p7_seg_queue_destroy(oai_p7_seg_queue_t* q);

// ---- Threads: spawn the P7 UDP listener + the rx_task. Started on
//      PNF_START.response, stopped on session reset / shutdown. ----
int  oai_p7_threads_start(oai_vnf_t* v);
void oai_p7_threads_stop(oai_vnf_t* v);

#endif /* OAI_OCUDU */
#endif /* OAI_OCUDU_OAI_P7_H */
