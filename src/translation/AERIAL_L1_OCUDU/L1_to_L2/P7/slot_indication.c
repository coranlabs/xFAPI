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

// SCF SLOT.indication (0x82) -> OCUDU SLOT.indication (0x82).
//
// SCF wire: [8B SCF header][sfn:u16 LE][slot:u16 LE].
// OCUDU wire body: slot_point_extended (u8 numerology + u32 count) +
//                  time_point (i64 ns, system_clock).
//   count = sfn * nof_slots_per_frame + slot,  nof_slots_per_frame = 10 << mu.

#include "aerial_l1_to_l2_p7.h"

#ifdef AERIAL_OCUDU

#include <string.h>
#include <time.h>

#include "../../main/app_context.h"
#include "aerial_l1_to_l2_p5.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"

int aerial_l1l2_slot_indication(struct AppContext* ctx,
                                const uint8_t* scf_msg, uint32_t msg_len)
{
    if (scf_msg == NULL || msg_len < AERIAL_SCF_MSG_HDR_SIZE + 4u) {
        SM_Logs(LOG_WARN, _P7_,
                "[L1->L2 P7] SLOT.indication too short (%u bytes).", msg_len);
        return -1;
    }

    uint16_t sfn, slot;
    memcpy(&sfn,  scf_msg + AERIAL_SCF_MSG_HDR_SIZE,     2);
    memcpy(&slot, scf_msg + AERIAL_SCF_MSG_HDR_SIZE + 2, 2);

    int mu = ctx->aerial_ocudu_ctx.cell_numerology;
    if (mu < 0 || mu > 4) mu = 1;
    uint32_t slots_per_frame = 10u << mu;
    uint32_t count = (uint32_t)sfn * slots_per_frame + slot;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t now_ns = (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;

    uint8_t    body[13];
    ocudu_wr_t w;
    ocudu_wr_init(&w, body, sizeof(body));
    ocudu_wr_u8(&w, (uint8_t)mu);
    ocudu_wr_u32(&w, count);
    ocudu_wr_i64(&w, now_ns);
    if (w.error) {
        SM_Logs(LOG_ERROR, _P7_, "[L1->L2 P7] SLOT.indication encode overflow.");
        return -1;
    }

    return aerial_l2_xsm_put(ctx, OCUDU_FAPI_SLOT_INDICATION, body, w.off);
}

#endif /* AERIAL_OCUDU */
