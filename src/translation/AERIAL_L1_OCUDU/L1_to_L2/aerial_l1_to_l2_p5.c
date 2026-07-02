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

// AERIAL_OCUDU P5 L1->L2 dispatcher + xSM put helper. Builds an OCUDU FAPI
// message (header + body) and delivers it to OCUDU-L2 over the L2 xSM.

#include "aerial_l1_to_l2_p5.h"

#ifdef AERIAL_OCUDU

#include <string.h>
#include <time.h>

#include "../../main/app_context.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"

static uint64_t aerial_l1l2_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int aerial_l2_xsm_put(struct AppContext* ctx, uint8_t msg_type,
                     const void* body, uint32_t body_len)
{
    if (ctx == NULL) return -1;
    AERIALOCUDUContext* oc = &ctx->aerial_ocudu_ctx;
    if (oc->h_l2 == NULL) {
        SM_Logs(LOG_WARN, _XSM_,
                "[L1->L2 P5] L2 xSM not ready; dropping OCUDU msg_type=0x%02x.",
                msg_type);
        return -1;
    }

    uint32_t total = OCUDU_XSM_HEADER_SIZE + body_len;

    uint64_t dst_pa = 0;
    xsm_status_t a_st = XSM_AcquireBuffer(oc->h_l2, &dst_pa);
    if (a_st != XSM_OK) {
        SM_Logs(LOG_WARN, _XSM_,
                "[L1->L2 P5] XSM_AcquireBuffer(L2) failed: %s; dropping "
                "msg_type=0x%02x.", xsm_strerror(a_st), msg_type);
        return -1;
    }
    void* dst_va = XSM_PhysToVirt(oc->h_l2, dst_pa);
    if (dst_va == NULL) {
        SM_Logs(LOG_ERROR, _XSM_,
                "[L1->L2 P5] PA->VA failed for L2 pa=0x%lx; dropping.",
                (unsigned long)dst_pa);
        XSM_ReturnBuffer(oc->h_l2, dst_pa);
        return -1;
    }

    ocudu_xsm_hdr_build((uint8_t*)dst_va, msg_type, body_len, aerial_l1l2_now_ns());
    if (body != NULL && body_len > 0) {
        memcpy((uint8_t*)dst_va + OCUDU_XSM_HEADER_SIZE, body, body_len);
    }

    xsm_msg_t out;
    memset(&out, 0, sizeof(out));
    out.payload_pa   = dst_pa;
    out.payload_size = total;       // full header + body, as OCUDU expects
    out.type_id      = msg_type;    // OCUDU msg_type mirrored in the descriptor
    out.flags        = 0;

    xsm_status_t p_st = XSM_Put(oc->h_l2, &out);
    if (p_st != XSM_OK) {
        SM_Logs(LOG_WARN, _XSM_,
                "[L1->L2 P5] XSM_Put(L2) failed: %s; returning buffer.",
                xsm_strerror(p_st));
        XSM_ReturnBuffer(oc->h_l2, dst_pa);
        return -1;
    }
    XSM_Notify(oc->h_l2);
    return 0;
}

int aerial_p5_send_response_to_l2(struct AppContext* ctx,
                                 uint8_t ocudu_msg_type, uint8_t error_code)
{
    switch (ocudu_msg_type) {
        case OCUDU_FAPI_PARAM_RESPONSE:
            return aerial_l1l2_param_response(ctx, error_code);
        case OCUDU_FAPI_CONFIG_RESPONSE:
            return aerial_l1l2_config_response(ctx, error_code);
        default:
            SM_Logs(LOG_WARN, _P5_,
                    "[L1->L2 P5] no translator for OCUDU msg_type=0x%02x.",
                    ocudu_msg_type);
            return -1;
    }
}

#endif /* AERIAL_OCUDU */
