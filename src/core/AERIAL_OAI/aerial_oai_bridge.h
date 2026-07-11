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

// AERIAL_OAI re-frame bridge. Aerial speaks little-endian SCF FAPI; OAI speaks
// big-endian nFAPI. Both encode the same open-nFAPI message structs, so a
// message is bridged by unpacking it with one codec into the struct and packing
// the struct with the other codec — no OCUDU intermediate representation.
//
//   Aerial -> OAI : LE-unpack SCF  -> struct -> BE-pack nFAPI -> SCTP/UDP
//   OAI -> Aerial : BE-unpack nFAPI -> struct -> LE-pack SCF   -> nvIPC

#ifndef AERIAL_OAI_BRIDGE_H
#define AERIAL_OAI_BRIDGE_H

#ifdef AERIAL_OAI

#include <stdint.h>

struct AppContext;
struct oai_pnf;

// Aerial -> OAI, P5 responses. scf_msg/scf_len is the whole SCF message from
// nvIPC (8-byte SCF prologue + body). Re-frames to big-endian nFAPI and sends it
// to the VNF over SCTP. Returns 0 on success, -1 on error.
int aerial_oai_bridge_p5_to_oai(struct AppContext* ctx, int32_t msg_id,
                                const uint8_t* scf_msg, uint32_t scf_len);

// Aerial -> OAI, P7 indications. scf_msg/scf_len is the SCF message; data_buf/
// data_len is the companion nvIPC data payload (RX_DATA transport blocks).
// Re-frames to nFAPI and sends it to the VNF over UDP. Returns 0/-1.
int aerial_oai_bridge_p7_to_oai(struct AppContext* ctx, int32_t msg_id,
                                const uint8_t* scf_msg, uint32_t scf_len,
                                const uint8_t* data_buf, uint32_t data_len);

// OAI -> Aerial, P5 requests (PARAM/CONFIG/START/STOP.request). nfapi_msg/len is
// the full big-endian nFAPI message from SCTP. Re-frames to SCF and sends it to
// Aerial over nvIPC. Returns 0/-1.
int aerial_oai_bridge_p5_to_aerial(struct AppContext* ctx,
                                   const uint8_t* nfapi_msg, uint32_t len);

// OAI -> Aerial, P7 requests (DL_TTI/UL_TTI/UL_DCI/TX_DATA.request). nfapi_msg/
// len is the reassembled big-endian nFAPI message from UDP. Re-frames to SCF and
// sends it to Aerial over nvIPC. Returns 0/-1.
int aerial_oai_bridge_p7_to_aerial(struct AppContext* ctx,
                                   const uint8_t* nfapi_msg, uint32_t len);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_BRIDGE_H */
