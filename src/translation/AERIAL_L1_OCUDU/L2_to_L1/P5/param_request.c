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

// OCUDU PARAM.request (0x00) is answered locally: Aerial's on_param_request()
// only logs "PARAM.req is not supported yet" and never replies, so forwarding
// it would stall OCUDU-L2 before CONFIG.request.

#include "aerial_l2_to_l1_p5.h"

#ifdef AERIAL_OCUDU

#include "aerial_l1_to_l2_p5.h"
#include "unified_logger.h"

#define AERIAL_PARAM_RESPONSE_OK 0

int aerial_l2l1_param_request(struct AppContext* ctx,
                             const uint8_t* body, uint32_t body_len)
{
    (void)body;
    (void)body_len;  // PARAM.request has no body in either dialect.

    SM_Logs(LOG_INFO, _P5_,
            "[L2->L1 P5] PARAM.request answered locally (Aerial does not "
            "implement PARAM.req); replying PARAM.response to OCUDU-L2.");

    return aerial_l1l2_param_response(ctx, AERIAL_PARAM_RESPONSE_OK);
}

#endif /* AERIAL_OCUDU */
