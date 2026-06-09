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

// OCUDU PARAM.request (0x00, empty body) -> nFAPI PARAM.request.

#include "oai_l2_to_l1_p5.h"

#ifdef OAI_OCUDU

#include "oai_vnf.h"
#include "unified_logger.h"
#include "nfapi_nr_interface_scf.h"

int ocudu_l2l1_param_request(struct oai_vnf* v,
                             const uint8_t* body, uint32_t body_len)
{
    (void)body;
    (void)body_len;  // PARAM.request has no body in either dialect.

    nfapi_nr_param_request_scf_t req;
    memset(&req, 0, sizeof(req));
    req.header.message_id = NFAPI_NR_PHY_MSG_TYPE_PARAM_REQUEST;
    req.header.phy_id     = (uint16_t)v->phy_id;

    return oai_vnf_send_p5_msg(v, &req.header, sizeof(req));
}

#endif /* OAI_OCUDU */
