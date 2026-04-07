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

#ifndef OCUDU_FORWARDER_SPLIT_L2_H
#define OCUDU_FORWARDER_SPLIT_L2_H

struct AppContext;

/* Split-mode (role=L2) forwarder threads. Two threads:
 *
 *   l2_to_eth: drain h_l2 (memzone SLAVE on pair 1, toward OCUDU-L2) and
 *              relay messages onto h_eth (DPDK-Ethernet handle, toward
 *              the peer XFAPI on the L1 server).
 *
 *   eth_to_l2: drain h_eth and relay messages onto h_l2.
 *
 * Both gated by ctx->ocudu_ctx.forwarders_running. Caller must have
 * opened h_l2 and h_eth and confirmed peer readiness on both before
 * calling _start. */
int  ocudu_fwd_split_l2_start(struct AppContext* ctx);
void ocudu_fwd_split_l2_stop(struct AppContext* ctx);

#endif /* OCUDU_FORWARDER_SPLIT_L2_H */
