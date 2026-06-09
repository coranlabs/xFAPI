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

// OCUDU CONFIG.request (0x02) -> nFAPI CONFIG.request (SCF).
// Parses OCUDU fapi::cell_configuration and fills nfapi_nr_config_request_scf_t.

#include "oai_l2_to_l1_p5.h"

#ifdef OAI_OCUDU

#include <stdlib.h>
#include <stdio.h>

#include "app_context.h"
#include "oai_vnf.h"
#include "ocudu_fapi_wire.h"
#include "unified_logger.h"
#include "nfapi_nr_interface_scf.h"

#define OAI_VNF_DEFAULT_TIMING_WINDOW 30

// slots-per-frame per numerology (mu 0..4). Matches the codec's table; the
// codec packs the TDD table over exactly this many slots.
static const uint8_t SLOTS_PER_FRAME[5] = {10, 20, 40, 80, 160};
#define NR_SYMBOLS_PER_SLOT 14  // normal CP (codec assumes normal CP in CONFIG)
#define OCUDU_NOF_SSB_BEAMS 64

// OCUDU ssb_periodicity stores the literal milliseconds (ms5=5 .. ms160=160),
// but nFAPI ssb_table.ssb_period is an enum INDEX (0:ms5 1:ms10 2:ms20 3:ms40
// 4:ms80 5:ms160). Forwarding the raw ms (e.g. 10) put an out-of-range value on
// the wire. Map ms -> nFAPI enum here.
static uint8_t ocudu_ssb_period_ms_to_nfapi(uint16_t ms)
{
    switch (ms) {
        case 5:   return 0;
        case 10:  return 1;
        case 20:  return 2;
        case 40:  return 3;
        case 80:  return 4;
        case 160: return 5;
        default:  return 1;  // safe fallback: ms10
    }
}

// NR-ARFCN -> frequency in kHz (TS 38.104 5.4.2.1).
static uint32_t arfcn_to_khz(uint32_t arfcn)
{
    if (arfcn < 600000)        return 5u * arfcn;                       // 0..3GHz
    if (arfcn < 2016667)       return 3000000u + 15u * (arfcn - 600000);// 3..24.25GHz
    return 24250080u + 60u * (arfcn - 2016667);                        // >24.25GHz
}

// TDD DL/UL period (in slots, for numerology mu) -> nFAPI tdd_period enum
// (0:0.5 1:0.625 2:1 3:1.25 4:2 5:2.5 6:5 7:10 8:20 ms).
static uint8_t tdd_period_to_enum(uint32_t period_slots, uint8_t mu)
{
    uint32_t slots_per_ms = 1u << mu;                 // 2^mu slots per 1ms
    if (slots_per_ms == 0) slots_per_ms = 1;
    uint32_t milli = (period_slots * 1000u) / slots_per_ms;  // period in 1/1000 ms
    switch (milli) {
        case 500:   return 0;
        case 625:   return 1;
        case 1000:  return 2;
        case 1250:  return 3;
        case 2000:  return 4;
        case 2500:  return 5;
        case 5000:  return 6;
        case 10000: return 7;
        case 20000: return 8;
        default:    return 6;   // safe fallback: 5ms
    }
}

// Number of PRACH root sequences needed to cover 64 preambles, from Ncs
// (TS 38.211 Table 6.3.3.1-5 unrestricted L=839, Table 6.3.3.1-7 L=139).
static uint8_t prach_num_root_sequences(int is_l839, uint16_t zero_corr_conf)
{
    static const uint16_t ncs_l839[16] =
        {0, 13, 15, 18, 22, 26, 32, 38, 46, 59, 76, 93, 119, 167, 279, 419};
    static const uint16_t ncs_l139[16] =
        {0, 2, 4, 6, 8, 10, 12, 13, 15, 17, 19, 23, 27, 34, 46, 69};
    uint16_t L   = is_l839 ? 839 : 139;
    uint16_t idx = (zero_corr_conf < 16) ? zero_corr_conf : 0;
    uint16_t ncs = is_l839 ? ncs_l839[idx] : ncs_l139[idx];
    uint16_t per_seq = (ncs == 0) ? L : (L / ncs);
    if (per_seq == 0) per_seq = 1;
    uint16_t n = (uint16_t)((64 + per_seq - 1) / per_seq);
    if (n == 0) n = 1;
    if (n > 64) n = 64;
    return (uint8_t)n;
}

// Free heap allocated for the nFAPI config request (PRACH list + TDD table).
static void config_req_free(nfapi_nr_config_request_scf_t* req, int n_slots)
{
    if (req == NULL) return;
    if (req->prach_config.num_prach_fd_occasions_list) {
        free(req->prach_config.num_prach_fd_occasions_list);
        req->prach_config.num_prach_fd_occasions_list = NULL;
    }
    if (req->tdd_table.max_tdd_periodicity_list) {
        for (int i = 0; i < n_slots; i++) {
            free(req->tdd_table.max_tdd_periodicity_list[i].max_num_of_symbol_per_slot_list);
        }
        free(req->tdd_table.max_tdd_periodicity_list);
        req->tdd_table.max_tdd_periodicity_list = NULL;
    }
}

int ocudu_l2l1_config_request(struct oai_vnf* v,
                              const uint8_t* body, uint32_t body_len)
{
    ocudu_rd_t r;
    ocudu_rd_init(&r, body, body_len);

    // ---- Parse OCUDU fapi::cell_configuration (exact serializer order) ----
    uint8_t  scs_common = ocudu_rd_u8(&r);
    uint8_t  cp         = ocudu_rd_u8(&r);  (void)cp;
    uint16_t pci        = ocudu_rd_u16(&r);
    uint8_t  duplex     = ocudu_rd_u8(&r);

    // carrier_config
    uint16_t dl_bandwidth   = ocudu_rd_u16(&r);
    uint32_t dl_arfcn       = ocudu_rd_u32(&r);
    uint16_t dl_k0[5];      for (int i = 0; i < 5; i++) dl_k0[i]        = ocudu_rd_u16(&r);
    uint16_t dl_grid[5];    for (int i = 0; i < 5; i++) dl_grid[i]      = ocudu_rd_u16(&r);
    uint16_t num_tx_ant     = ocudu_rd_u16(&r);
    uint16_t ul_bandwidth   = ocudu_rd_u16(&r);
    uint32_t ul_arfcn       = ocudu_rd_u32(&r);
    uint16_t ul_k0[5];      for (int i = 0; i < 5; i++) ul_k0[i]        = ocudu_rd_u16(&r);
    uint16_t ul_grid[5];    for (int i = 0; i < 5; i++) ul_grid[i]      = ocudu_rd_u16(&r);
    uint16_t num_rx_ant     = ocudu_rd_u16(&r);
    uint8_t  freq_shift     = ocudu_rd_u8(&r);
    (void)ocudu_rd_u8(&r);  // power_profile
    (void)ocudu_rd_u8(&r);  // power_offset_rs_index
    (void)ocudu_rd_u8(&r);  // dmrs_typeA_pos

    // prach (rach_config_common)
    uint8_t  prach_cfg_index = ocudu_rd_u8(&r);
    (void)ocudu_rd_u32(&r);                          // ra_resp_window
    uint32_t msg1_fdm        = ocudu_rd_u32(&r);
    uint32_t msg1_freq_start = ocudu_rd_u32(&r);
    uint16_t zero_corr_conf  = ocudu_rd_u16(&r);
    (void)ocudu_rd_i32(&r);                          // preamble_rx_target_pw
    (void)ocudu_rd_u8(&r);                           // preamble_trans_max
    (void)ocudu_rd_u8(&r);                           // power_ramping_step_db
    (void)ocudu_rd_u32(&r);                          // total_nof_ra_preambles
    (void)ocudu_rd_i64(&r);                          // ra_con_res_timer (ms)
    uint8_t  is_l839         = (uint8_t)ocudu_rd_bool(&r);
    uint32_t prach_root_seq  = ocudu_rd_u32(&r);
    uint8_t  msg1_scs        = ocudu_rd_u8(&r);
    uint8_t  restricted_set  = ocudu_rd_u8(&r);
    (void)ocudu_rd_bool(&r);                         // msg3_transform_precoder
    (void)ocudu_rd_u8(&r);                           // nof_ssb_per_ro
    (void)ocudu_rd_u8(&r);                           // nof_cb_preambles_per_ssb
    uint32_t n_slices        = ocudu_rd_u32(&r);     // ra_prio_slice_info_list
    for (uint32_t s = 0; s < n_slices && !r.error; s++) {
        uint32_t nsag = ocudu_rd_u32(&r);
        ocudu_rd_skip(&r, nsag);                     // nsag_id_list bytes
        (void)ocudu_rd_u8(&r);                       // pwr_ramp_step_hi_prio
        if (ocudu_rd_u8(&r)) (void)ocudu_rd_u8(&r);  // optional scaling_bi
    }

    // ssb_configuration
    uint8_t  ssb_scs         = ocudu_rd_u8(&r);  (void)ssb_scs;
    uint16_t ssb_offset_pA   = ocudu_rd_u16(&r);
    uint16_t ssb_period_ocu  = ocudu_rd_u16(&r);
    uint8_t  k_ssb           = ocudu_rd_u8(&r);
    uint32_t bm_nbits        = ocudu_rd_u32(&r);
    uint64_t ssb_bitmap      = 0;
    for (uint32_t b = 0; b < bm_nbits; b += 64) {
        uint64_t chunk = ocudu_rd_u64(&r);
        if (b == 0) ssb_bitmap = chunk;              // first 64 SSBs
    }
    uint8_t beam_ids[OCUDU_NOF_SSB_BEAMS];
    ocudu_rd_bytes(&r, beam_ids, OCUDU_NOF_SSB_BEAMS);
    (void)ocudu_rd_u8(&r);                           // pss_to_sss_epre
    int32_t ssb_block_power  = ocudu_rd_i32(&r);

    // tdd_ul_dl_cfg_common (optional)
    uint8_t  has_tdd = ocudu_rd_u8(&r);
    uint32_t tdd_period_slots = 0, nof_dl_slots = 0, nof_dl_symbols = 0;
    uint32_t nof_ul_slots = 0, nof_ul_symbols = 0;
    if (has_tdd) {
        (void)ocudu_rd_u8(&r);                       // ref_scs
        tdd_period_slots = ocudu_rd_u32(&r);
        nof_dl_slots     = ocudu_rd_u32(&r);
        nof_dl_symbols   = ocudu_rd_u32(&r);
        nof_ul_slots     = ocudu_rd_u32(&r);
        nof_ul_symbols   = ocudu_rd_u32(&r);
        if (ocudu_rd_u8(&r)) {                        // pattern2 (summed into period)
            uint32_t p2_period = ocudu_rd_u32(&r);
            (void)ocudu_rd_u32(&r); (void)ocudu_rd_u32(&r);
            (void)ocudu_rd_u32(&r); (void)ocudu_rd_u32(&r);
            tdd_period_slots += p2_period;            // best-effort sum periodicity
        }
    }

    if (r.error) {
        SM_Logs(LOG_ERROR, _P5_,
                "[L2->L1 P5] CONFIG.request: OCUDU body underrun (len=%u).",
                body_len);
        return -1;
    }

    uint8_t mu = (scs_common < 5) ? scs_common : 1;
    int     n_slots = SLOTS_PER_FRAME[mu];

    // Latch the cell numerology for the P7 SLOT.indication translator
    // (nFAPI SLOT.ind carries sfn/slot but not mu).
    v->cell_numerology = (int)mu;
    // Latch the phys_cell_id for the P7 UL_TTI PRACH translator (OCUDU's UL
    // PRACH PDU carries no pci; the nFAPI PRACH PDU requires it).
    v->cell_pci = (int)pci;

    // ---- Build nFAPI CONFIG.request ----
    nfapi_nr_config_request_scf_t* req =
        (nfapi_nr_config_request_scf_t*)calloc(1, sizeof(*req));
    if (req == NULL) {
        SM_Logs(LOG_CRTERR, _P5_, "[L2->L1 P5] CONFIG.request: calloc failed.");
        return -1;
    }
    req->header.message_id = NFAPI_NR_PHY_MSG_TYPE_CONFIG_REQUEST;
    req->header.phy_id     = (uint16_t)v->phy_id;

    // carrier_config: OCUDU sends carrier CENTRE; nFAPI wants POINT A. Convert
    // centre -> Point A by subtracting half the carrier bandwidth (OAI re-derives
    // centre = bw/2 + dl_frequency), else OAI tunes bw/2 too high.
    uint32_t scs_khz        = (uint32_t)(15u << mu);
    uint32_t dl_half_bw_khz = (uint32_t)dl_grid[mu] * 6u * scs_khz;  // 12*grid*scs/2
    uint32_t ul_half_bw_khz = (uint32_t)ul_grid[mu] * 6u * scs_khz;
    uint32_t dl_point_a_khz = arfcn_to_khz(dl_arfcn) - dl_half_bw_khz;
    uint32_t ul_point_a_khz = arfcn_to_khz(ul_arfcn) - ul_half_bw_khz;
    req->carrier_config.dl_bandwidth.tl.tag = NFAPI_NR_CONFIG_DL_BANDWIDTH_TAG;
    req->carrier_config.dl_bandwidth.value  = dl_bandwidth;
    req->carrier_config.dl_frequency.tl.tag = NFAPI_NR_CONFIG_DL_FREQUENCY_TAG;
    req->carrier_config.dl_frequency.value  = dl_point_a_khz;
    req->carrier_config.uplink_bandwidth.tl.tag = NFAPI_NR_CONFIG_UPLINK_BANDWIDTH_TAG;
    req->carrier_config.uplink_bandwidth.value  = ul_bandwidth;
    req->carrier_config.uplink_frequency.tl.tag = NFAPI_NR_CONFIG_UPLINK_FREQUENCY_TAG;
    req->carrier_config.uplink_frequency.value  = ul_point_a_khz;
    req->carrier_config.num_tx_ant.tl.tag = NFAPI_NR_CONFIG_NUM_TX_ANT_TAG;
    req->carrier_config.num_tx_ant.value  = num_tx_ant;
    req->carrier_config.num_rx_ant.tl.tag = NFAPI_NR_CONFIG_NUM_RX_ANT_TAG;
    req->carrier_config.num_rx_ant.value  = num_rx_ant;
    req->carrier_config.frequency_shift_7p5khz.tl.tag = NFAPI_NR_CONFIG_FREQUENCY_SHIFT_7P5KHZ_TAG;
    // OCUDU leaves freq_shift_7p5kHz uninitialised; force 0 to match native VNF.
    (void)freq_shift;
    req->carrier_config.frequency_shift_7p5khz.value  = 0;
    for (int i = 0; i < 5; i++) {
        // OCUDU leaves ul_k0 uninitialised; mirror dl_k0 into ul_k0 (TDD: equal).
        req->carrier_config.dl_k0[i].value       = dl_k0[i];
        req->carrier_config.dl_grid_size[i].value = dl_grid[i];
        req->carrier_config.ul_k0[i].value        = dl_k0[i];
        req->carrier_config.ul_grid_size[i].value = ul_grid[i];
    }
    (void)ul_k0;

    // cell_config
    req->cell_config.phy_cell_id.tl.tag = NFAPI_NR_CONFIG_PHY_CELL_ID_TAG;
    req->cell_config.phy_cell_id.value  = pci;
    req->cell_config.frame_duplex_type.tl.tag = NFAPI_NR_CONFIG_FRAME_DUPLEX_TYPE_TAG;
    req->cell_config.frame_duplex_type.value  = duplex;  // 0=FDD 1=TDD

    // ssb_config
    req->ssb_config.ss_pbch_power.tl.tag = NFAPI_NR_CONFIG_SS_PBCH_POWER_TAG;
    req->ssb_config.ss_pbch_power.value  = ssb_block_power;
    req->ssb_config.bch_payload.tl.tag = NFAPI_NR_CONFIG_BCH_PAYLOAD_TAG;
    // 1 = PHY generates timing PBCH bits (we provide only the 24-bit MIB).
    req->ssb_config.bch_payload.value  = 1;
    req->ssb_config.scs_common.tl.tag = NFAPI_NR_CONFIG_SCS_COMMON_TAG;
    req->ssb_config.scs_common.value  = scs_common;

    // ssb_table
    req->ssb_table.ssb_offset_point_a.tl.tag = NFAPI_NR_CONFIG_SSB_OFFSET_POINT_A_TAG;
    req->ssb_table.ssb_offset_point_a.value  = ssb_offset_pA;
    req->ssb_table.ssb_subcarrier_offset.tl.tag = NFAPI_NR_CONFIG_SSB_SUBCARRIER_OFFSET_TAG;
    req->ssb_table.ssb_subcarrier_offset.value  = k_ssb;
    req->ssb_table.ssb_period.tl.tag = NFAPI_NR_CONFIG_SSB_PERIOD_TAG;
    req->ssb_table.ssb_period.value  = ocudu_ssb_period_ms_to_nfapi(ssb_period_ocu);
    // SSB mask bit-order: OCUDU ssb_bitmap is LSB-first; nFAPI ssb_mask is
    // MSB-first (SSB 0 = mask0 bit 31). Convert or OAI places SSB at wrong index.
    {
        uint32_t m0 = 0, m1 = 0;
        for (int i = 0; i < 64; ++i) {
            if (!((ssb_bitmap >> i) & 1ull)) continue;
            if (i < 32) m0 |= (uint32_t)1u << (31 - i);      // SSB 0..31  -> mask0 MSB-first
            else        m1 |= (uint32_t)1u << (63 - i);      // SSB 32..63 -> mask1 MSB-first
        }
        req->ssb_table.ssb_mask_list[0].ssb_mask.tl.tag = NFAPI_NR_CONFIG_SSB_MASK_TAG;
        req->ssb_table.ssb_mask_list[0].ssb_mask.value  = m0;
        req->ssb_table.ssb_mask_list[1].ssb_mask.tl.tag = NFAPI_NR_CONFIG_SSB_MASK_TAG;
        req->ssb_table.ssb_mask_list[1].ssb_mask.value  = m1;
    }
    // SSB case_v3 vendor TLV: OCUDU carries no case, derive from scs like OAI's
    // set_ssb_case (30kHz->C(2) for n78); wrong case hides the SSB from the UE.
    {
        uint8_t ssb_case;
        switch (scs_common) {
            case 0:  ssb_case = 0; break;              // 15 kHz  -> Case A
            case 1:  ssb_case = 2; break;              // 30 kHz  -> Case C (n78)
            case 3:  ssb_case = 3; break;              // 120 kHz -> Case D
            case 4:  ssb_case = 4; break;              // 240 kHz -> Case E
            default: ssb_case = 2; break;
        }
        req->ssb_table.case_v3.tl.tag = NFAPI_NR_FAPI_SSB_CASE_VENDOR_EXTENSION_TAG;
        req->ssb_table.case_v3.value  = ssb_case;
    }

    // prach_config
    uint8_t n_fd = (uint8_t)((msg1_fdm >= 1 && msg1_fdm <= 8) ? msg1_fdm : 1);
    req->prach_config.prach_sequence_length.tl.tag = NFAPI_NR_CONFIG_PRACH_SEQUENCE_LENGTH_TAG;
    req->prach_config.prach_sequence_length.value  = is_l839 ? 0 : 1;  // 0=long 1=short
    req->prach_config.prach_sub_c_spacing.tl.tag = NFAPI_NR_CONFIG_PRACH_SUB_C_SPACING_TAG;
    req->prach_config.prach_sub_c_spacing.value  = msg1_scs;
    req->prach_config.restricted_set_config.tl.tag = NFAPI_NR_CONFIG_RESTRICTED_SET_CONFIG_TAG;
    req->prach_config.restricted_set_config.value  = restricted_set;
    req->prach_config.num_prach_fd_occasions.tl.tag = NFAPI_NR_CONFIG_NUM_PRACH_FD_OCCASIONS_TAG;
    req->prach_config.num_prach_fd_occasions.value  = n_fd;
    req->prach_config.prach_ConfigurationIndex.tl.tag = NFAPI_NR_CONFIG_PRACH_CONFIG_INDEX_TAG;
    req->prach_config.prach_ConfigurationIndex.value  = prach_cfg_index;
    req->prach_config.ssb_per_rach.tl.tag = NFAPI_NR_CONFIG_SSB_PER_RACH_TAG;
    req->prach_config.ssb_per_rach.value  = 3;  // 1 SSB per RACH occasion (default)

    uint8_t num_root = prach_num_root_sequences(is_l839, zero_corr_conf);
    req->prach_config.num_prach_fd_occasions_list =
        (nfapi_nr_num_prach_fd_occasions_t*)calloc(n_fd, sizeof(nfapi_nr_num_prach_fd_occasions_t));
    if (req->prach_config.num_prach_fd_occasions_list == NULL) {
        SM_Logs(LOG_CRTERR, _P5_, "[L2->L1 P5] CONFIG.request: PRACH list calloc failed.");
        free(req);
        return -1;
    }
    for (int i = 0; i < n_fd; i++) {
        nfapi_nr_num_prach_fd_occasions_t* occ =
            &req->prach_config.num_prach_fd_occasions_list[i];
        occ->prach_root_sequence_index.tl.tag = NFAPI_NR_CONFIG_PRACH_ROOT_SEQUENCE_INDEX_TAG;
        occ->prach_root_sequence_index.value  = (uint16_t)prach_root_seq;
        occ->num_root_sequences.tl.tag = NFAPI_NR_CONFIG_NUM_ROOT_SEQUENCES_TAG;
        occ->num_root_sequences.value  = num_root;
        occ->k1.tl.tag = NFAPI_NR_CONFIG_K1_TAG;
        occ->k1.value  = (uint16_t)msg1_freq_start;
        occ->prach_zero_corr_conf.tl.tag = NFAPI_NR_CONFIG_PRACH_ZERO_CORR_CONF_TAG;
        occ->prach_zero_corr_conf.value  = (uint8_t)zero_corr_conf;
        occ->num_unused_root_sequences.tl.tag = NFAPI_NR_CONFIG_NUM_UNUSED_ROOT_SEQUENCES_TAG;
        occ->num_unused_root_sequences.value  = 0;
        occ->unused_root_sequences_list = NULL;
    }

    // tdd_table: required for TDD so OAI can allocate + read the per-slot table.
    if (duplex == 1 /* TDD */ && has_tdd && tdd_period_slots > 0) {
        req->tdd_table.tdd_period.tl.tag = NFAPI_NR_CONFIG_TDD_PERIOD_TAG;
        req->tdd_table.tdd_period.value  = tdd_period_to_enum(tdd_period_slots, mu);
        req->tdd_table.max_tdd_periodicity_list =
            (nfapi_nr_max_tdd_periodicity_t*)calloc(n_slots, sizeof(nfapi_nr_max_tdd_periodicity_t));
        if (req->tdd_table.max_tdd_periodicity_list == NULL) {
            SM_Logs(LOG_CRTERR, _P5_, "[L2->L1 P5] CONFIG.request: TDD list calloc failed.");
            config_req_free(req, 0);
            free(req);
            return -1;
        }
        for (int s = 0; s < n_slots; s++) {
            req->tdd_table.max_tdd_periodicity_list[s].max_num_of_symbol_per_slot_list =
                (nfapi_nr_max_num_of_symbol_per_slot_t*)calloc(
                    NR_SYMBOLS_PER_SLOT, sizeof(nfapi_nr_max_num_of_symbol_per_slot_t));
            if (req->tdd_table.max_tdd_periodicity_list[s].max_num_of_symbol_per_slot_list == NULL) {
                SM_Logs(LOG_CRTERR, _P5_, "[L2->L1 P5] CONFIG.request: TDD slot calloc failed.");
                // free already-allocated slots
                for (int q = 0; q < s; q++)
                    free(req->tdd_table.max_tdd_periodicity_list[q].max_num_of_symbol_per_slot_list);
                free(req->tdd_table.max_tdd_periodicity_list);
                req->tdd_table.max_tdd_periodicity_list = NULL;
                config_req_free(req, 0);
                free(req);
                return -1;
            }
        }
        // Expand the OCUDU DL/UL pattern into per-symbol slot_config (0=DL 1=UL
        // 2=guard) for one period, then tile it across the whole frame.
        uint32_t period = tdd_period_slots;
        for (int s = 0; s < n_slots; s++) {
            uint32_t ps = (uint32_t)s % period;      // slot within the period
            nfapi_nr_max_num_of_symbol_per_slot_t* syms =
                req->tdd_table.max_tdd_periodicity_list[s].max_num_of_symbol_per_slot_list;
            for (int k = 0; k < NR_SYMBOLS_PER_SLOT; k++) {
                uint8_t cfg;
                if (ps < nof_dl_slots) {
                    cfg = 0;                          // full DL slot
                } else if (ps >= period - nof_ul_slots) {
                    cfg = 1;                          // full UL slot
                } else if (ps == nof_dl_slots) {
                    // special slot: DL symbols, guard, UL symbols
                    if ((uint32_t)k < nof_dl_symbols)
                        cfg = 0;
                    else if ((uint32_t)k >= (uint32_t)(NR_SYMBOLS_PER_SLOT - (int)nof_ul_symbols))
                        cfg = 1;
                    else
                        cfg = 2;                      // guard
                } else {
                    cfg = 2;                          // flexible/guard
                }
                syms[k].slot_config.tl.tag = NFAPI_NR_CONFIG_SLOT_CONFIG_TAG;
                syms[k].slot_config.value  = cfg;
            }
        }
    }

    // nfapi_config: tell the PNF where to send P7 (VNF P7 addr/port) + timing
    // window, sourced from xFAPI socket config (OCUDU FAPI carries none).
    const char* vnf_ip = (v->ctx != NULL) ? v->ctx->config.nfapi_socket.local_ip : NULL;
    int vnf_p7_port    = (v->ctx != NULL) ? v->ctx->config.nfapi_socket.p7_local_port : 0;
    if (vnf_ip != NULL && vnf_ip[0] != '\0') {
        req->nfapi_config.p7_vnf_address_ipv4.tl.tag = NFAPI_NR_NFAPI_P7_VNF_ADDRESS_IPV4_TAG;
        sscanf(vnf_ip, "%hhu.%hhu.%hhu.%hhu",
               &req->nfapi_config.p7_vnf_address_ipv4.address[0],
               &req->nfapi_config.p7_vnf_address_ipv4.address[1],
               &req->nfapi_config.p7_vnf_address_ipv4.address[2],
               &req->nfapi_config.p7_vnf_address_ipv4.address[3]);
    }
    req->nfapi_config.p7_vnf_port.tl.tag = NFAPI_NR_NFAPI_P7_VNF_PORT_TAG;
    req->nfapi_config.p7_vnf_port.value  = (uint16_t)vnf_p7_port;
    req->nfapi_config.timing_window.tl.tag = NFAPI_NR_NFAPI_TIMING_WINDOW_TAG;
    req->nfapi_config.timing_window.value  = OAI_VNF_DEFAULT_TIMING_WINDOW;

    int rc = oai_vnf_send_p5_msg(v, &req->header, sizeof(*req));

    config_req_free(req, n_slots);
    free(req);
    return rc;
}

#endif /* OAI_OCUDU */
