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

#include "aerial_send.h"

#ifdef AERIAL_OAI

#include <stdlib.h>
#include <string.h>

#include "../../main/app_context.h"
#include "aerial_nvipc.h"
#include "unified_logger.h"

#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"
#include "nfapi.h"
#include "nr_fapi.h"
#include "nr_fapi_p5.h"
#include "nr_fapi_p7.h"

#define AERIAL_P5_TX_BUF_SIZE 15000u
#define AERIAL_P7_MSG_BUF_SIZE 15000u
#define AERIAL_P7_DATA_BUF_SIZE 576000u

int aerial_send_p5_msg(struct AppContext* ctx, void* nfapi_p5_hdr,
                       uint32_t msg_len)
{
    if (ctx == NULL || nfapi_p5_hdr == NULL) {
        return -1;
    }
    nfapi_nr_p4_p5_message_header_t* hdr =
        (nfapi_nr_p4_p5_message_header_t*)nfapi_p5_hdr;

    uint8_t packed[AERIAL_P5_TX_BUF_SIZE];
    int packed_len = fapi_nr_p5_message_pack(nfapi_p5_hdr, msg_len,
                                             packed, sizeof(packed), NULL);
    if (packed_len <= 0) {
        SM_Logs(LOG_ERROR, _P5_,
                "[L2->L1 P5] SCF pack failed for msg_id=0x%02x (rc=%d).",
                hdr->message_id, packed_len);
        return -1;
    }

    SM_Logs(LOG_INFO, _P5_,
            "[L2->L1 P5] msg_id=0x%02x -> Aerial (msg_len=%d).",
            hdr->message_id, packed_len);

    // P5 packer return value already includes the 8-byte SCF prologue.
    return aerial_nvipc_send(ctx, (int32_t)hdr->message_id,
                             ctx->aerial_oai_ctx.cell_id,
                             packed, (uint32_t)packed_len, NULL, 0);
}

// Aerial expects TX_DATA transport blocks in data_buf, 4-byte aligned, PDU_length
// rewritten to the packed size, TLV tag forced to 2.
static int aerial_pack_tx_data(nfapi_nr_tx_data_request_t* req,
                               uint8_t* data_buf, uint32_t data_buf_len,
                               int32_t* data_len)
{
    uint8_t*  data_end   = data_buf + data_buf_len;
    uint8_t*  write_ptr  = data_buf;
    uint8_t** ppWriteData = &write_ptr;
    const int32_t data_buf_len32 = (int32_t)((data_buf_len + 3) / 4);

    for (int i = 0; i < req->Number_of_PDUs; i++) {
        nfapi_nr_pdu_t* pdu = &req->pdu_list[i];
        pdu->PDU_length = pdu->TLVs[0].length;
        for (uint32_t k = 0; k < pdu->num_TLV; ++k) {
            pdu->TLVs[k].tag = 2;
            uint32_t len = pdu->TLVs[k].length;
            if (len == 0) continue;
            uint32_t num_values = (len + 3) / 4;
            if (len % 4 != 0) {
                if (!pusharray32(pdu->TLVs[k].value.direct, data_buf_len32,
                                 num_values - 1, ppWriteData, data_end)) {
                    return -1;
                }
                int bytes_to_add = 4 - (4 - (len % 4)) % 4;
                if (bytes_to_add != 4) {
                    for (int j = 0; j < bytes_to_add; j++) {
                        uint8_t b = (uint8_t)(pdu->TLVs[k].value.direct[num_values - 1] >> (j * 8));
                        if (!push8(b, ppWriteData, data_end)) return -1;
                    }
                }
            } else if (!pusharray32(pdu->TLVs[k].value.direct, data_buf_len32,
                                    num_values, ppWriteData, data_end)) {
                return -1;
            }
        }
    }
    *data_len = (int32_t)(write_ptr - data_buf);
    return 0;
}

int aerial_send_p7_msg(struct AppContext* ctx, void* nfapi_p7_hdr)
{
    if (ctx == NULL || nfapi_p7_hdr == NULL) {
        return -1;
    }
    nfapi_nr_p7_message_header_t* hdr = (nfapi_nr_p7_message_header_t*)nfapi_p7_hdr;

    uint8_t* msg_buf = malloc(AERIAL_P7_MSG_BUF_SIZE);
    if (msg_buf == NULL) {
        return -1;
    }

    uint8_t* data_buf = NULL;
    int32_t  data_len = 0;
    if (hdr->message_id == NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST) {
        data_buf = malloc(AERIAL_P7_DATA_BUF_SIZE);
        if (data_buf == NULL) {
            free(msg_buf);
            return -1;
        }
        if (aerial_pack_tx_data((nfapi_nr_tx_data_request_t*)hdr,
                                data_buf, AERIAL_P7_DATA_BUF_SIZE,
                                &data_len) != 0) {
            SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] TX_DATA payload pack failed.");
            free(data_buf); free(msg_buf);
            return -1;
        }
    }

    int body_len = fapi_nr_p7_message_pack(hdr, msg_buf,
                                           AERIAL_P7_MSG_BUF_SIZE, NULL);
    if (body_len <= 0) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L2->L1 P7] SCF pack failed for msg_id=0x%02x (rc=%d).",
                hdr->message_id, body_len);
        free(data_buf); free(msg_buf);
        return -1;
    }

    // P7 packer returns body length only (unlike P5); add the 8-byte prologue.
    uint32_t msg_len = (uint32_t)body_len + 8u;

    int rc = aerial_nvipc_send(ctx, (int32_t)hdr->message_id,
                               ctx->aerial_oai_ctx.cell_id,
                               msg_buf, msg_len,
                               data_buf, (uint32_t)data_len);
    free(data_buf);
    free(msg_buf);
    return rc;
}

#endif /* AERIAL_OAI */
