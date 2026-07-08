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

// SCF UCI.indication -> OCUDU UCI_INDICATION (0x87).
// OCUDU variant order: uci_pusch_pdu(0), uci_pucch_pdu_format_0_1(1),
// uci_pucch_pdu_format_2_3_4(2), matching the SCF pdu_type values.
// rsrp 0xFFFF = invalid; timing_advance_offset emitted absent (see crc_indication.c).

#include "aerial_l1_to_l2_p7.h"

#ifdef AERIAL_OCUDU

#include <stdio.h>

#include "app_context.h"
#include "aerial_l1_to_l2_p5.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "message_stats.h"

// PUCCH 0/1 and 2/3/4 pduBitmap: bit0 SR, bit1 HARQ, bit2 CSI-1, bit3 CSI-2.
// PUSCH pduBitmap leaves bit0 unused.
#define UCI_BIT_SR    0x01
#define UCI_BIT_HARQ  0x02
#define UCI_BIT_CSI1  0x04
#define UCI_BIT_CSI2  0x08

// SCF harq_value {0=pass,1=fail,2=not present} -> OCUDU {nack=0,ack=1,dtx=2}.
static const uint8_t HARQ_VAL_MAP[3] = { 1, 0, 2 };

// SCF crc {0=pass,1=failure} -> OCUDU detection_status {crc_pass=1,crc_failure=2}.
static uint8_t crc_to_detection_status(uint8_t crc)
{
    return (crc == 0) ? 1u : 2u;
}

// OCUDU bounded_bitset: u32 nbits + ceil(nbits/64) LE u64 chunks. The SCF payload
// is byte-packed LSB-first, so bit i lands in chunk[i/64] at bit i%64.
static void wr_bitset_from_bytes(ocudu_wr_t* w, const uint8_t* p, uint32_t nbits)
{
    ocudu_wr_u32(w, nbits);
    for (uint32_t base = 0; base < nbits; base += 64) {
        uint32_t chunk_bits = (nbits - base < 64u) ? (nbits - base) : 64u;
        uint64_t chunk = 0;
        for (uint32_t b = 0; b < chunk_bits; ++b) {
            uint32_t i = base + b;
            if (p[i >> 3] & (1u << (i & 7u))) chunk |= (1ULL << b);
        }
        ocudu_wr_u64(w, chunk);
    }
}

// optional {detection_status:enum_u8, expected_bit_length:u32, payload:bitset}
static void wr_opt_report(ocudu_wr_t* w, int present, uint8_t crc,
                          uint16_t bit_len, const uint8_t* payload)
{
    if (!present || (bit_len > 0 && payload == NULL)) { ocudu_wr_u8(w, 0); return; }
    ocudu_wr_u8(w, 1);
    ocudu_wr_u8(w, crc_to_detection_status(crc));
    ocudu_wr_u32(w, bit_len);
    wr_bitset_from_bytes(w, payload, bit_len);
}

// Common trailer of every OCUDU UCI PDU: sinr, absent TA, rssi, invalid rsrp.
static void wr_meas(ocudu_wr_t* w, uint8_t ul_cqi, uint16_t rssi)
{
    ocudu_wr_u16(w, (uint16_t)aerial_cqi_to_ul_sinr_metric(ul_cqi));
    ocudu_wr_u8(w, 0);
    ocudu_wr_u16(w, rssi);
    ocudu_wr_u16(w, 0xFFFF);
}

static void wr_pusch_pdu(ocudu_wr_t* w, const nfapi_nr_uci_pusch_pdu_t* p)
{
    ocudu_wr_u8(w, 0);
    ocudu_wr_u32(w, p->handle);
    ocudu_wr_u16(w, p->rnti);
    wr_meas(w, p->ul_cqi, p->rssi);
    wr_opt_report(w, p->pduBitmap & UCI_BIT_HARQ, p->harq.harq_crc,
                  p->harq.harq_bit_len, p->harq.harq_payload);
    wr_opt_report(w, p->pduBitmap & UCI_BIT_CSI1, p->csi_part1.csi_part1_crc,
                  p->csi_part1.csi_part1_bit_len, p->csi_part1.csi_part1_payload);
    wr_opt_report(w, p->pduBitmap & UCI_BIT_CSI2, p->csi_part2.csi_part2_crc,
                  p->csi_part2.csi_part2_bit_len, p->csi_part2.csi_part2_payload);
}

static void wr_pucch_0_1_pdu(ocudu_wr_t* w, const nfapi_nr_uci_pucch_pdu_format_0_1_t* p)
{
    ocudu_wr_u8(w, 1);
    ocudu_wr_u32(w, p->handle);
    ocudu_wr_u16(w, p->rnti);
    ocudu_wr_u8(w, p->pucch_format);
    wr_meas(w, p->ul_cqi, p->rssi);

    // optional sr: sr_pdu_format_0_1 { bool sr_detected }
    if (p->pduBitmap & UCI_BIT_SR) {
        ocudu_wr_u8(w, 1);
        ocudu_wr_u8(w, p->sr.sr_indication ? 1 : 0);
    } else {
        ocudu_wr_u8(w, 0);
    }

    // optional harq: uci_harq_format_0_1 { u16 count, count x enum_u8 }
    if (p->pduBitmap & UCI_BIT_HARQ) {
        uint16_t nh = (p->harq.num_harq > 2) ? 2 : p->harq.num_harq;
        ocudu_wr_u8(w, 1);
        ocudu_wr_u16(w, nh);
        for (uint16_t h = 0; h < nh; ++h) {
            uint8_t v = p->harq.harq_list[h].harq_value;
            ocudu_wr_u8(w, (v < 3) ? HARQ_VAL_MAP[v] : 2);
        }
    } else {
        ocudu_wr_u8(w, 0);
    }
}

static void wr_pucch_2_3_4_pdu(ocudu_wr_t* w, const nfapi_nr_uci_pucch_pdu_format_2_3_4_t* p)
{
    ocudu_wr_u8(w, 2);
    ocudu_wr_u32(w, p->handle);
    ocudu_wr_u16(w, p->rnti);
    ocudu_wr_u8(w, p->pucch_format);
    wr_meas(w, p->ul_cqi, p->rssi);

    // optional sr: sr_pdu_format_2_3_4 { bounded_bitset sr_payload }
    if ((p->pduBitmap & UCI_BIT_SR) && p->sr.sr_payload != NULL) {
        ocudu_wr_u8(w, 1);
        wr_bitset_from_bytes(w, p->sr.sr_payload, p->sr.sr_bit_len);
    } else {
        ocudu_wr_u8(w, 0);
    }

    wr_opt_report(w, p->pduBitmap & UCI_BIT_HARQ, p->harq.harq_crc,
                  p->harq.harq_bit_len, p->harq.harq_payload);
    wr_opt_report(w, p->pduBitmap & UCI_BIT_CSI1, p->csi_part1.csi_part1_crc,
                  p->csi_part1.csi_part1_bit_len, p->csi_part1.csi_part1_payload);
    wr_opt_report(w, p->pduBitmap & UCI_BIT_CSI2, p->csi_part2.csi_part2_crc,
                  p->csi_part2.csi_part2_bit_len, p->csi_part2.csi_part2_payload);
}

int aerial_l1l2_uci_indication(struct AppContext* ctx,
                               const nfapi_nr_uci_indication_t* ind)
{
    if (ind == NULL || (ind->num_ucis > 0 && ind->uci_list == NULL)) {
        return -1;
    }

    int      mu    = aerial_p7_cell_mu(ctx);
    uint32_t count = (uint32_t)ind->sfn * (10u << mu) + ind->slot;

    uint8_t    body[8192];
    ocudu_wr_t w;
    ocudu_wr_init(&w, body, sizeof(body));

    ocudu_wr_u8(&w, 1);
    ocudu_wr_u8(&w, (uint8_t)mu);
    ocudu_wr_u32(&w, count);
    ocudu_wr_u16(&w, ind->num_ucis);

    char content[MAX_MESSAGE_CONTENT_LEN];
    int  clen = snprintf(content, sizeof(content),
                         "message_type=UCI_INDICATION\nsfn=%u\nslot=%u\nnum_ucis=%u\n",
                         ind->sfn, ind->slot, ind->num_ucis);

    for (uint16_t i = 0; i < ind->num_ucis; ++i) {
        const nfapi_nr_uci_t* u = &ind->uci_list[i];

        switch (u->pdu_type) {
            case NFAPI_NR_UCI_PUSCH_PDU_TYPE:
                wr_pusch_pdu(&w, &u->pusch_pdu);
                break;
            case NFAPI_NR_UCI_FORMAT_0_1_PDU_TYPE:
                wr_pucch_0_1_pdu(&w, &u->pucch_pdu_format_0_1);
                break;
            case NFAPI_NR_UCI_FORMAT_2_3_4_PDU_TYPE:
                wr_pucch_2_3_4_pdu(&w, &u->pucch_pdu_format_2_3_4);
                break;
            default:
                SM_Logs(LOG_WARN, _P7_,
                        "[L1->L2 P7] UCI.ind unknown pdu_type=%u; dropping indication.",
                        u->pdu_type);
                return -1;
        }

        if (clen >= 0 && clen < (int)sizeof(content))
            clen += snprintf(content + clen, sizeof(content) - clen,
                             "----UCI[%u]----\npdu_type=%u\n", i, u->pdu_type);
    }

    if (w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] UCI.indication encode overflow (num_ucis=%u).",
                ind->num_ucis);
        return -1;
    }

    add_message_stats("UCI_INDICATION", (int)ind->sfn, (int)ind->slot,
                      (int)w.off, (int)ind->num_ucis, content, 0);

    return aerial_l2_xsm_put(ctx, OCUDU_FAPI_UCI_INDICATION, body, w.off);
}

#endif /* AERIAL_OCUDU */
