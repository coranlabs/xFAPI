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

// nFAPI UCI.indication (0x87) -> OCUDU UCI_INDICATION.
//
// nFAPI wire (BIG-endian, after pdu_type:u16 pdu_size:u16):
//   pdu_type=1 (PUCCH 0/1): pduBitmap:u8 (bit0 SR, bit1 HARQ), handle:u32, rnti:u16,
//     pucch_format:u8, ul_cqi:u8, timing_advance:u16, rssi:u16,
//     [if SR: sr_indication:u8, sr_confidence:u8],
//     [if HARQ: num_harq:u8, harq_confidence:u8, num_harq x harq_value:u8]
//   pdu_type=2 (PUCCH 2/3/4): pduBitmap:u8 (bit0 SR,1 HARQ,2 CSI1,3 CSI2), handle:u32,
//     rnti:u16, pucch_format:u8, ul_cqi:u8, timing_advance:u16, rssi:u16, then per
//     present report: SR{bit_len:u16, payload}; HARQ/CSI1/CSI2 {crc:u8, bit_len:u16, payload}.
//   pdu_type=0 (PUSCH UCI) not emitted in this config; dropped if seen.
//
// OCUDU variant order: uci_pusch_pdu(0), uci_pucch_pdu_format_0_1(1),
//   uci_pucch_pdu_format_2_3_4(2). ul_sinr_metric <- (ul_cqi-128)*250; rsrp 0xFFFF=invalid.
//   sr_detected <- (sr_indication != 0). HARQ remap nFAPI{0=pass,1=fail,2=np} ->
//   OCUDU{nack=0,ack=1,dtx=2}: pass->ack(1), fail->nack(0), np->dtx(2) (un-remapped inverts ACK/NACK).

#include "oai_l1_to_l2_p7.h"

#ifdef OAI_OCUDU

#include <stdio.h>

#include "../../main/app_context.h"
#include "oai_vnf.h"
#include "oai_l1_to_l2_p5.h"     // ocudu_l2_xsm_put()
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "nfapi_nr_interface.h"  // NFAPI_NR_P7_HEADER_LENGTH
#include "message_stats.h"

#define NFAPI_UCI_PUSCH_PDU_TYPE     0
#define NFAPI_UCI_PUCCH_0_1_PDU_TYPE 1
#define NFAPI_UCI_PUCCH_2_3_4_PDU_TYPE 2
#define OCUDU_UCI_VARIANT_PUCCH_0_1  1   // index in std::variant
#define OCUDU_UCI_VARIANT_PUCCH_2_3_4 2  // index in std::variant
// nFAPI UCI 2/3/4 pduBitmap bits.
#define NFAPI_UCI_234_BIT_SR    0x01
#define NFAPI_UCI_234_BIT_HARQ  0x02
#define NFAPI_UCI_234_BIT_CSI1  0x04
#define NFAPI_UCI_234_BIT_CSI2  0x08
// nFAPI harq_value -> OCUDU uci_pucch_f0_or_f1_harq_values {nack=0,ack=1,dtx=2}.
static const uint8_t HARQ_VAL_MAP[3] = { 1 /*pass->ack*/, 0 /*fail->nack*/, 2 /*np->dtx*/ };

typedef struct { const uint8_t* d; uint32_t len; uint32_t off; int err; } be_rd_t;

static uint8_t be_u8(be_rd_t* r)
{
    if (r->err || r->off + 1u > r->len) { r->err = 1; return 0; }
    return r->d[r->off++];
}
static uint16_t be_u16(be_rd_t* r)
{
    if (r->err || r->off + 2u > r->len) { r->err = 1; return 0; }
    uint16_t v = (uint16_t)((r->d[r->off] << 8) | r->d[r->off + 1]);
    r->off += 2; return v;
}
static uint32_t be_u32(be_rd_t* r)
{
    if (r->err || r->off + 4u > r->len) { r->err = 1; return 0; }
    uint32_t v = ((uint32_t)r->d[r->off] << 24) | ((uint32_t)r->d[r->off + 1] << 16)
               | ((uint32_t)r->d[r->off + 2] << 8) | (uint32_t)r->d[r->off + 3];
    r->off += 4; return v;
}

static int16_t cqi_to_ul_sinr_metric(uint8_t ul_cqi)
{
    int32_t m = ((int32_t)ul_cqi - 128) * 250;   // SNR_dB*500 (0.002 dB step)
    if (m >  32767) m =  32767;
    if (m < -32767) m = -32767;
    return (int16_t)m;
}

// Return a pointer to n bytes at the read offset and advance past them; NULL on overrun.
static const uint8_t* be_bytes(be_rd_t* r, uint32_t n)
{
    if (r->err || r->off + n > r->len) { r->err = 1; return NULL; }
    const uint8_t* p = r->d + r->off;
    r->off += n;
    return p;
}

// nFAPI UCI 2/3/4 CRC flag (0=pass,1=failure) -> OCUDU detection_status{crc_pass=1,crc_failure=2}.
static uint8_t crc_to_detection_status(uint8_t nfapi_crc)
{
    return (nfapi_crc == 0) ? 1u /*crc_pass*/ : 2u /*crc_failure*/;
}

// OCUDU bounded_bitset: u32 nbits + ceil(nbits/64) LE u64 chunks (bit i -> chunk[i/64]
// bit i%64). nFAPI payload is byte-packed LSB-first (same indexing). p NULL iff nbits==0.
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

// One nFAPI UCI 2/3/4 report {crc:u8, bit_len:u16, payload} -> OCUDU optional report
// {flag:u8, detection_status:enum_u8, expected_bit_length:u32, payload:bitset}.
static void xlate_uci_234_report(be_rd_t* r, ocudu_wr_t* w)
{
    uint8_t  crc     = be_u8(r);
    uint16_t bit_len = be_u16(r);
    const uint8_t* payload = be_bytes(r, (uint32_t)((bit_len + 7u) / 8u));
    if (r->err) return;
    ocudu_wr_u8(w, 1);                                 // optional present
    ocudu_wr_u8(w, crc_to_detection_status(crc));      // detection_status enum_u8
    ocudu_wr_u32(w, bit_len);                          // expected_bit_length (units::bits)
    wr_bitset_from_bytes(w, payload, bit_len);         // payload bitset
}

int ocudu_l1l2_uci_indication(struct AppContext* ctx,
                              const uint8_t* nfapi_msg, uint32_t len)
{
    if (nfapi_msg == NULL || len < NFAPI_NR_P7_HEADER_LENGTH + 6u) {
        SM_Logs(LOG_WARN, _P7_, "[L1->L2 P7] UCI.indication too short (%u).", len);
        return -1;
    }

    be_rd_t r = { nfapi_msg + NFAPI_NR_P7_HEADER_LENGTH,
                  len - NFAPI_NR_P7_HEADER_LENGTH, 0, 0 };
    uint16_t sfn   = be_u16(&r);
    uint16_t slot  = be_u16(&r);
    uint16_t n_uci = be_u16(&r);

    oai_vnf_t* v = ctx->oai_ocudu_ctx.vnf;
    int mu = (v != NULL && v->cell_numerology >= 0 && v->cell_numerology < 5)
                 ? v->cell_numerology : 1;
    uint32_t slots_per_frame = 10u << mu;
    uint32_t count = (uint32_t)sfn * slots_per_frame + slot;

    uint8_t    body[4096];
    ocudu_wr_t w;
    ocudu_wr_init(&w, body, sizeof(body));

    ocudu_wr_u8(&w, 1);
    ocudu_wr_u8(&w, (uint8_t)mu);
    ocudu_wr_u32(&w, count);
    ocudu_wr_u16(&w, n_uci);

    char content[MAX_MESSAGE_CONTENT_LEN];
    int  clen = snprintf(content, sizeof(content),
                         "message_type=UCI_INDICATION\nsfn=%u\nslot=%u\nnum_ucis=%u\n",
                         sfn, slot, n_uci);

    for (uint16_t i = 0; i < n_uci && !r.err; ++i) {
        uint16_t pdu_type = be_u16(&r);
        uint16_t pdu_size = be_u16(&r);  (void)pdu_size;

        // PUCCH format 2/3/4 UCI -> OCUDU variant index 2 = uci_pucch_pdu_format_2_3_4.
        if (pdu_type == NFAPI_UCI_PUCCH_2_3_4_PDU_TYPE) {
            uint8_t  pduBitmap = be_u8(&r);
            uint32_t handle    = be_u32(&r);
            uint16_t rnti      = be_u16(&r);
            uint8_t  pucch_fmt = be_u8(&r);
            uint8_t  ul_cqi    = be_u8(&r);
            uint16_t ta        = be_u16(&r);  (void)ta;
            uint16_t rssi      = be_u16(&r);

            ocudu_wr_u8(&w, OCUDU_UCI_VARIANT_PUCCH_2_3_4);
            ocudu_wr_u32(&w, handle);
            ocudu_wr_u16(&w, rnti);
            ocudu_wr_u8(&w, pucch_fmt);                                  // enum_u8 (0=F2,1=F3,2=F4)
            ocudu_wr_u16(&w, (uint16_t)cqi_to_ul_sinr_metric(ul_cqi));   // i16 ul_sinr_metric
            ocudu_wr_u8(&w, 0);                                          // timing_advance_offset: absent
            ocudu_wr_u16(&w, rssi);                                      // rssi
            ocudu_wr_u16(&w, 0xFFFF);                                    // rsrp: invalid

            // sr: optional<sr_pdu_format_2_3_4{ bounded_bitset<4> sr_payload }>
            if (pduBitmap & NFAPI_UCI_234_BIT_SR) {
                uint16_t sr_bit_len = be_u16(&r);
                const uint8_t* sr_p = be_bytes(&r, (uint32_t)((sr_bit_len + 7u) / 8u));
                if (!r.err) { ocudu_wr_u8(&w, 1); wr_bitset_from_bytes(&w, sr_p, sr_bit_len); }
            } else { ocudu_wr_u8(&w, 0); }
            // harq / csi_part1 / csi_part2: optional {crc, bit_len, payload}
            if (pduBitmap & NFAPI_UCI_234_BIT_HARQ) { xlate_uci_234_report(&r, &w); }
            else { ocudu_wr_u8(&w, 0); }
            if (pduBitmap & NFAPI_UCI_234_BIT_CSI1) { xlate_uci_234_report(&r, &w); }
            else { ocudu_wr_u8(&w, 0); }
            if (pduBitmap & NFAPI_UCI_234_BIT_CSI2) { xlate_uci_234_report(&r, &w); }
            else { ocudu_wr_u8(&w, 0); }
            if (r.err) break;

            if (clen >= 0 && clen < (int)sizeof(content))
                clen += snprintf(content + clen, sizeof(content) - clen,
                                 "----UCI[%u]----\npdu_type=2\nhandle=%u\nrnti=%u\n"
                                 "pucch_format=%u\nul_cqi=%u\nrssi=%u\nbitmap=0x%02x\n",
                                 i, handle, rnti, pucch_fmt, ul_cqi, rssi, pduBitmap);
            continue;
        }

        if (pdu_type != NFAPI_UCI_PUCCH_0_1_PDU_TYPE) {
            SM_Logs(LOG_WARN, _P7_,
                    "[L1->L2 P7] UCI.ind unsupported pdu_type=%u (PUSCH UCI not "
                    "translated); dropping indication.", pdu_type);
            r.err = 1;
            break;
        }

        uint8_t  pduBitmap = be_u8(&r);
        uint32_t handle    = be_u32(&r);
        uint16_t rnti      = be_u16(&r);
        uint8_t  pucch_fmt = be_u8(&r);
        uint8_t  ul_cqi    = be_u8(&r);
        uint16_t ta        = be_u16(&r);  (void)ta;
        uint16_t rssi      = be_u16(&r);

        int      sr_present   = (pduBitmap & 0x01) ? 1 : 0;
        int      harq_present = ((pduBitmap >> 1) & 0x01) ? 1 : 0;
        uint8_t  sr_ind = 0, sr_conf = 0;
        if (sr_present) { sr_ind = be_u8(&r); sr_conf = be_u8(&r); (void)sr_conf; }
        uint8_t  num_harq = 0, harq_conf = 0;
        uint8_t  harq_vals[2] = {0, 0};
        if (harq_present) {
            num_harq  = be_u8(&r);
            harq_conf = be_u8(&r);  (void)harq_conf;
            for (uint8_t h = 0; h < num_harq; ++h) {
                uint8_t hv = be_u8(&r);
                if (h < 2) harq_vals[h] = hv;
            }
        }
        if (r.err) break;

        // OCUDU variant: index 1 = uci_pucch_pdu_format_0_1.
        ocudu_wr_u8(&w, OCUDU_UCI_VARIANT_PUCCH_0_1);
        ocudu_wr_u32(&w, handle);
        ocudu_wr_u16(&w, rnti);
        ocudu_wr_u8(&w, pucch_fmt);                                  // enum_u8 pucch_format
        ocudu_wr_u16(&w, (uint16_t)cqi_to_ul_sinr_metric(ul_cqi));   // i16 ul_sinr_metric
        ocudu_wr_u8(&w, 0);                                          // timing_advance_offset: absent
        ocudu_wr_u16(&w, rssi);                                      // rssi (SCF passthrough)
        ocudu_wr_u16(&w, 0xFFFF);                                    // rsrp: invalid
        // optional sr (sr_pdu_format_0_1: bool sr_detected)
        if (sr_present) { ocudu_wr_u8(&w, 1); ocudu_wr_u8(&w, sr_ind ? 1 : 0); }
        else            { ocudu_wr_u8(&w, 0); }
        // optional harq (uci_harq_format_0_1: u16 count + count x enum_u8)
        if (harq_present) {
            uint16_t nh = (num_harq > 2) ? 2 : num_harq;            // OCUDU MAX_NUM_HARQ=2
            ocudu_wr_u8(&w, 1);
            ocudu_wr_u16(&w, nh);
            for (uint16_t h = 0; h < nh; ++h) {
                uint8_t nf = harq_vals[h];
                ocudu_wr_u8(&w, (nf < 3) ? HARQ_VAL_MAP[nf] : 2 /*dtx*/);
            }
        } else {
            ocudu_wr_u8(&w, 0);
        }

        if (clen >= 0 && clen < (int)sizeof(content))
            clen += snprintf(content + clen, sizeof(content) - clen,
                             "----UCI[%u]----\npdu_type=%u\nhandle=%u\nrnti=%u\n"
                             "pucch_format=%u\nul_cqi=%u\nrssi=%u\nsr=%d(ind=%u)\n"
                             "harq=%d(num=%u v0=%u v1=%u)\n",
                             i, pdu_type, handle, rnti, pucch_fmt, ul_cqi, rssi,
                             sr_present, sr_ind, harq_present, num_harq,
                             harq_vals[0], harq_vals[1]);
    }

    if (r.err || w.error) {
        SM_Logs(LOG_ERROR, _P7_,
                "[L1->L2 P7] UCI.indication parse/encode error "
                "(rd_off=%u rd_len=%u wr_off=%u n_uci=%u).",
                r.off, r.len, w.off, n_uci);
        return -1;
    }

    add_message_stats("UCI_INDICATION", (int)sfn, (int)slot,
                      (int)w.off, (int)n_uci, content, 0);

    return ocudu_l2_xsm_put(ctx, OCUDU_FAPI_UCI_INDICATION, body, w.off);
}

#endif /* OAI_OCUDU */
