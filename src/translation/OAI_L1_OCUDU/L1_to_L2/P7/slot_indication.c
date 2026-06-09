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

// nFAPI SLOT.indication -> OCUDU SLOT.indication (0x82).
//
// nFAPI wire: [18B NR P7 header][sfn:u16 BE][slot:u16 BE].
// OCUDU wire body: slot_point_extended (u8 numerology + u32 count) +
//                  time_point (i64 ns, system_clock).
//   count = sfn * nof_slots_per_frame + slot,  nof_slots_per_frame = 10 << mu.
// (mu = cell numerology, latched from CONFIG.request.)

#include "oai_l1_to_l2_p7.h"

#ifdef OAI_OCUDU

#include <time.h>

#include <stdio.h>

#include "../../main/app_context.h"
#include "oai_vnf.h"
#include "oai_l1_to_l2_p5.h"     // ocudu_l2_xsm_put()
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "nfapi_nr_interface.h"  // NFAPI_NR_P7_HEADER_LENGTH
#include "message_stats.h"

int ocudu_l1l2_slot_indication(struct AppContext* ctx,
                               const uint8_t* nfapi_msg, uint32_t len)
{
    if (nfapi_msg == NULL || len < NFAPI_NR_P7_HEADER_LENGTH + 4u) {
        SM_Logs(LOG_WARN, _P7_,
                "[L1->L2 P7] SLOT.indication too short (%u bytes).", len);
        return -1;
    }

    // sfn/slot follow the 18-byte NR P7 header, packed big-endian by the codec.
    const uint8_t* b = nfapi_msg + NFAPI_NR_P7_HEADER_LENGTH;
    uint16_t sfn  = (uint16_t)((b[0] << 8) | b[1]);
    uint16_t slot = (uint16_t)((b[2] << 8) | b[3]);

    oai_vnf_t* v = ctx->oai_ocudu_ctx.vnf;
    int mu = (v != NULL && v->cell_numerology >= 0 && v->cell_numerology < 5)
                 ? v->cell_numerology : 1;
    uint32_t slots_per_frame = 10u << mu;            // 10 * 2^mu
    uint32_t count = (uint32_t)sfn * slots_per_frame + slot;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t now_ns = (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;

    // OCUDU slot_indication body (13 bytes).
    uint8_t    body[13];
    ocudu_wr_t w;
    ocudu_wr_init(&w, body, sizeof(body));
    ocudu_wr_u8(&w, (uint8_t)mu);     // slot_point_extended numerology
    ocudu_wr_u32(&w, count);          // slot_point_extended count
    ocudu_wr_i64(&w, now_ns);         // time_point (ns since epoch)
    if (w.error) {
        SM_Logs(LOG_ERROR, _P7_, "[L1->L2 P7] SLOT.indication encode overflow.");
        return -1;
    }

    // Record for the XFAPI dashboard (L1->L2 indication; no nFAPI struct built).
    {
        char content[MAX_MESSAGE_CONTENT_LEN];
        snprintf(content, sizeof(content),
                 "message_type=SLOT_INDICATION\n"
                 "sfn=%u\n"
                 "slot=%u\n"
                 "numerology=%d\n"
                 "count=%u\n"
                 "time_point_ns=%lld\n",
                 sfn, slot, mu, count, (long long)now_ns);
        add_message_stats("SLOT_INDICATION", (int)sfn, (int)slot,
                          (int)w.off, 1, content, 0);
    }

    return ocudu_l2_xsm_put(ctx, OCUDU_FAPI_SLOT_INDICATION, body, w.off);
}

#endif /* OAI_OCUDU */
