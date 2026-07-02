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

// OCUDU STOP.request (0x05, empty body) -> nFAPI STOP.request.

#include "aerial_l2_to_l1_p5.h"

#ifdef AERIAL_OCUDU

#include "aerial_send.h"
#include "unified_logger.h"
#include "nfapi_nr_interface_scf.h"

int aerial_l2l1_stop_request(struct AppContext* ctx,
                            const uint8_t* body, uint32_t body_len)
{
    (void)body;
    (void)body_len;  // STOP.request has no body.

    nfapi_nr_stop_request_scf_t req;
    memset(&req, 0, sizeof(req));
    req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_STOP_REQUEST;
    req.header.phy_id     = 0;

    return aerial_send_p5_msg(ctx, &req.header, sizeof(req));
}

#endif /* AERIAL_OCUDU */
