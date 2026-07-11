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
// Owns the P5 (SCTP) handshake responder and the P7 (UDP) data plane. The wire
// is big-endian nFAPI, decoded/encoded via the be_* codec entry points.

#ifndef AERIAL_OAI_OAI_PNF_H
#define AERIAL_OAI_OAI_PNF_H

#ifdef AERIAL_OAI

#include <stdint.h>

struct AppContext;

// Allocate + initialize PNF state, create+bind the SCTP P5 listener and the P7
// UDP socket, and start the P5 listener thread. The listener accepts the VNF
// (OAI L2), RESPONDS to the PNF handshake (PNF_PARAM/CONFIG/START.request ->
// .response), and on RUNNING starts the P7 listener + rx_task and the
// OAI->Aerial forwarder. Returns 0 on success, -1 on failure.
int  oai_pnf_start(struct AppContext* ctx);

// Stop all PNF threads, close sockets, free state. Idempotent.
void oai_pnf_stop(struct AppContext* ctx);

// ---------------------------------------------------------------------------
// Bridge entry points (Aerial <-> OAI, no OCUDU intermediate representation).
// ---------------------------------------------------------------------------

// Aerial -> OAI: called by the nvIPC RX thread for every SCF FAPI message from
// Aerial. Re-frames the message (unpack SCF little-endian -> struct -> pack
// nFAPI big-endian) and sends it to the OAI VNF over P5/P7. scf_msg/scf_len is
// nv_ipc_msg_t.msg_buf; data_buf/data_len is the companion data payload (RX_DATA
// transport blocks). Returns 0 on success, -1 on error.
int  aerial_oai_from_aerial(struct AppContext* ctx, int32_t msg_id,
                            const uint8_t* scf_msg, uint32_t scf_len,
                            const uint8_t* data_buf, uint32_t data_len);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_OAI_PNF_H */
