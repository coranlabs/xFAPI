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

// AERIAL_OCUDU P7 L2->L1: UL_TTI.request.
// Walks all sub-PDU variants in wire order (prach, pusch, pucch, srs); PDUs are
// emitted only if the walk consumes the body EXACTLY (off == body_len).

#include "aerial_l2_to_l1_p7.h"

#ifdef AERIAL_OCUDU

#include <stdlib.h>
#include <string.h>

#include "ocudu_fapi_wire.h"
#include "app_context.h"
#include "aerial_send.h"
#include "unified_logger.h"
#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"
#include "message_stats.h"

#define OCUDU_UL_TTI_NUM_PDU_TYPES 6
#define OCUDU_UL_PDU_TYPE_PRACH        0
#define OCUDU_UL_PDU_TYPE_PUSCH        1
#define OCUDU_UL_PDU_TYPE_PUCCH_F01    2
#define OCUDU_UL_PDU_TYPE_PUCCH_F234   3
#define OCUDU_UL_PDU_TYPE_SRS          4
#define OCUDU_UL_PDU_TYPE_MSGA_PUSCH   5

#define OCUDU_PUSCH_RB_BITMAP_BYTES    36
#define OCUDU_PUSCH_SPATIAL_STREAMS    64

// ---- capture structs ----

typedef struct {
    uint8_t  num_prach_ocas, prach_format, index_fd_ra, prach_start_symbol;
    uint16_t num_cs;
    uint32_t handle;
    uint8_t  num_fd_ra, preamble_start, preamble_stop;
} cap_prach_t;

typedef struct {
    uint64_t pdu_bitmap;
    uint16_t rnti;
    uint32_t handle;
    uint32_t bwp_start, bwp_stop;
    uint8_t  scs, cp;
    uint16_t target_code_rate;
    uint8_t  qam, mcs, mcs_table, transform_precoding;
    uint16_t nid_pusch;
    uint8_t  num_layers;
    uint16_t ul_dmrs_symb_pos;
    uint8_t  dmrs_type;
    uint16_t dmrs_scram_id, dmrs_scram_id_compl;
    uint8_t  low_papr;
    uint16_t dmrs_identity;
    uint8_t  nscid, num_cdm_no_data;
    uint16_t dmrs_ports;
    uint8_t  resource_alloc;
    uint8_t  rb_bitmap[OCUDU_PUSCH_RB_BITMAP_BYTES];
    uint32_t vrb_start, vrb_stop;
    uint8_t  vrb_to_prb, intra_hop;
    uint16_t tx_dc;
    uint8_t  ufs_7p5;
    uint8_t  sym_start, sym_stop;
    // pusch_data
    uint8_t  rv_index, harq_id, new_data;
    uint32_t tb_size;
    uint16_t num_cb;
    // pusch_uci
    uint16_t harq_ack_bits, csi1_bits, csi2_flags;
    uint8_t  alpha, beta_harq, beta_csi1, beta_csi2;
    // dfts_ofdm
    uint8_t  dfts_grp; uint16_t dfts_seq; uint8_t dfts_sample_den, dfts_time_den;
    // maintenance v3
    uint8_t  group_seq_hop;        // group_or_sequence_hopping (0=neither 1=group 2=seq)
    uint8_t  ldpc_base_graph;
    uint32_t tb_size_lbrm;
} cap_pusch_t;

typedef struct {
    uint16_t rnti;
    uint32_t handle;
    uint32_t bwp_start, bwp_stop;
    uint8_t  scs, cp, format_type, multi_slot, pi2_bpsk;
    uint32_t prb_start, prb_stop;
    uint8_t  sym_start, sym_stop, intra_hop;
    uint16_t second_hop_prb;
    uint8_t  grp_hopping;
    uint16_t nid_hopping, init_cyclic_shift, nid_scrambling;
    uint8_t  time_occ, pre_dft_idx, pre_dft_len, add_dmrs;
    uint16_t nid0_dmrs;
    uint8_t  m0_shift, sr_bit_len;
    uint16_t bit_len_harq, csi1_bits;
} cap_pucch_t;

typedef struct {
    uint16_t rnti;
    uint32_t handle;
    uint32_t bwp_start, bwp_stop;
    uint8_t  scs, cp, num_ant_ports, sym_start, sym_stop, num_repetitions;
    uint8_t  time_start, config_index;
    uint16_t sequence_id;
    uint8_t  bandwidth_index, comb_size, comb_offset, cyclic_shift, freq_position;
    uint16_t freq_shift;
    uint8_t  freq_hopping, grp_seq_hopping, resource_type;
    uint16_t t_srs, t_offset;
} cap_srs_t;

static void walk_uci_corr(ocudu_rd_t* r)
{
    uint16_t count = ocudu_rd_u16(r);
    for (uint16_t i = 0; i < count; ++i) {
        ocudu_rd_u16(r);
        ocudu_rd_skip_sv_u16(r);
        ocudu_rd_skip_sv_u8(r);
        ocudu_rd_u16(r);
        ocudu_rd_u8(r);
    }
}

static void read_prach(ocudu_rd_t* r, cap_prach_t* o)
{
    cap_prach_t t;
    t.num_prach_ocas     = ocudu_rd_u8(r);
    t.prach_format       = ocudu_rd_u8(r);
    t.index_fd_ra        = ocudu_rd_u8(r);
    t.prach_start_symbol = ocudu_rd_u8(r);
    t.num_cs             = ocudu_rd_u16(r);
    t.handle             = ocudu_rd_u32(r);
    t.num_fd_ra          = ocudu_rd_u8(r);
    t.preamble_start     = ocudu_rd_u8(r);
    t.preamble_stop      = ocudu_rd_u8(r);
    if (o) *o = t;
}

static void walk_pusch(ocudu_rd_t* r, cap_pusch_t* o)
{
    cap_pusch_t t;
    t.pdu_bitmap = ocudu_rd_u64(r);
    t.rnti       = ocudu_rd_u16(r);
    t.handle     = ocudu_rd_u32(r);
    t.bwp_start  = ocudu_rd_u32(r); t.bwp_stop = ocudu_rd_u32(r);
    t.scs        = ocudu_rd_u8(r);
    t.cp         = ocudu_rd_u8(r);
    t.target_code_rate    = ocudu_rd_u16(r);
    t.qam        = ocudu_rd_u8(r);
    t.mcs        = ocudu_rd_u8(r);
    t.mcs_table  = ocudu_rd_u8(r);
    t.transform_precoding = ocudu_rd_u8(r);
    t.nid_pusch  = ocudu_rd_u16(r);
    t.num_layers = ocudu_rd_u8(r);
    t.ul_dmrs_symb_pos = ocudu_rd_u16(r);
    t.dmrs_type  = ocudu_rd_u8(r);
    t.dmrs_scram_id        = ocudu_rd_u16(r);
    t.dmrs_scram_id_compl  = ocudu_rd_u16(r);
    t.low_papr   = ocudu_rd_u8(r);
    t.dmrs_identity = ocudu_rd_u16(r);
    t.nscid      = ocudu_rd_u8(r);
    t.num_cdm_no_data = ocudu_rd_u8(r);
    t.dmrs_ports = ocudu_rd_u16(r);
    t.resource_alloc = ocudu_rd_u8(r);
    ocudu_rd_bytes(r, t.rb_bitmap, OCUDU_PUSCH_RB_BITMAP_BYTES);
    t.vrb_start  = ocudu_rd_u32(r); t.vrb_stop = ocudu_rd_u32(r);
    t.vrb_to_prb = ocudu_rd_u8(r);
    t.intra_hop  = ocudu_rd_u8(r);
    t.tx_dc      = ocudu_rd_u16(r);
    t.ufs_7p5    = ocudu_rd_u8(r);
    t.sym_start  = ocudu_rd_u8(r); t.sym_stop = ocudu_rd_u8(r);
    // pusch_data
    t.rv_index   = ocudu_rd_u8(r);
    t.harq_id    = ocudu_rd_u8(r);
    t.new_data   = ocudu_rd_u8(r);
    t.tb_size    = ocudu_rd_u32(r);
    t.num_cb     = ocudu_rd_u16(r);
    ocudu_rd_skip_sv_u8(r);               // cb_present_and_position (CBG; not mapped)
    // pusch_uci
    t.harq_ack_bits = ocudu_rd_u16(r);
    t.csi1_bits  = ocudu_rd_u16(r);
    t.csi2_flags = ocudu_rd_u16(r);
    t.alpha      = ocudu_rd_u8(r);
    t.beta_harq  = ocudu_rd_u8(r);
    t.beta_csi1  = ocudu_rd_u8(r);
    t.beta_csi2  = ocudu_rd_u8(r);
    // pusch_ptrs (not mapped: nFAPI num_ptrs_ports=0)
    uint16_t nof_ports = ocudu_rd_u16(r);
    for (uint16_t i = 0; i < nof_ports; ++i) { ocudu_rd_u16(r); ocudu_rd_u8(r); ocudu_rd_u8(r); }
    ocudu_rd_u8(r); ocudu_rd_u8(r); ocudu_rd_u8(r);
    // pusch_dfts_ofdm
    t.dfts_grp        = ocudu_rd_u8(r);
    t.dfts_seq        = ocudu_rd_u16(r);
    t.dfts_sample_den = ocudu_rd_u8(r);
    t.dfts_time_den   = ocudu_rd_u8(r);
    // pusch_maintenance_v3: trans_type(u8), delta_bwp0(u16), initial_ul_bwp_size(u16),
    // group_or_sequence_hopping(u8), pusch_second_hop_prb(u16), ldpc(u8), tb_lbrm(u32)
    ocudu_rd_u8(r); ocudu_rd_u16(r); ocudu_rd_u16(r);
    t.group_seq_hop = ocudu_rd_u8(r);
    ocudu_rd_u16(r);
    t.ldpc_base_graph = ocudu_rd_u8(r);
    t.tb_size_lbrm    = ocudu_rd_u32(r);
    // pusch_params_v4
    ocudu_rd_u8(r); ocudu_rd_u32(r); ocudu_rd_u8(r); ocudu_rd_u8(r);
    ocudu_rd_skip(r, OCUDU_PUSCH_SPATIAL_STREAMS);
    walk_uci_corr(r);
    uint8_t has_ctx = ocudu_rd_u8(r);
    if (has_ctx) { ocudu_rd_u16(r); ocudu_rd_u8(r); }
    if (o) *o = t;
}

static void walk_pucch(ocudu_rd_t* r, cap_pucch_t* o)
{
    cap_pucch_t t;
    t.rnti       = ocudu_rd_u16(r);
    t.handle     = ocudu_rd_u32(r);
    t.bwp_start  = ocudu_rd_u32(r); t.bwp_stop = ocudu_rd_u32(r);
    t.scs        = ocudu_rd_u8(r);
    t.cp         = ocudu_rd_u8(r);
    t.format_type = ocudu_rd_u8(r);
    t.multi_slot  = ocudu_rd_u8(r);
    t.pi2_bpsk    = ocudu_rd_u8(r);
    t.prb_start  = ocudu_rd_u32(r); t.prb_stop = ocudu_rd_u32(r);
    t.sym_start  = ocudu_rd_u8(r); t.sym_stop = ocudu_rd_u8(r);
    t.intra_hop  = ocudu_rd_u8(r);
    t.second_hop_prb = ocudu_rd_u16(r);
    t.grp_hopping = ocudu_rd_u8(r);
    ocudu_rd_u8(r);                        // reserved
    t.nid_hopping       = ocudu_rd_u16(r);
    t.init_cyclic_shift = ocudu_rd_u16(r);
    t.nid_scrambling    = ocudu_rd_u16(r);
    t.time_occ   = ocudu_rd_u8(r);
    t.pre_dft_idx = ocudu_rd_u8(r);
    t.pre_dft_len = ocudu_rd_u8(r);
    t.add_dmrs   = ocudu_rd_u8(r);
    t.nid0_dmrs  = ocudu_rd_u16(r);
    t.m0_shift   = ocudu_rd_u8(r);
    t.sr_bit_len = ocudu_rd_u8(r);
    t.bit_len_harq = ocudu_rd_u16(r);
    t.csi1_bits  = ocudu_rd_u16(r);
    ocudu_rd_u8(r); ocudu_rd_u8(r);       // pucch_maintenance_v3 (max_code_rate, ul_bwp_id)
    walk_uci_corr(r);
    if (o) *o = t;
}

static void walk_srs(ocudu_rd_t* r, cap_srs_t* o)
{
    cap_srs_t t;
    t.rnti       = ocudu_rd_u16(r);
    t.handle     = ocudu_rd_u32(r);
    t.bwp_start  = ocudu_rd_u32(r); t.bwp_stop = ocudu_rd_u32(r);
    t.scs        = ocudu_rd_u8(r);
    t.cp         = ocudu_rd_u8(r);
    t.num_ant_ports = ocudu_rd_u8(r);
    t.sym_start  = ocudu_rd_u8(r); t.sym_stop = ocudu_rd_u8(r);
    t.num_repetitions = ocudu_rd_u8(r);
    t.time_start = ocudu_rd_u8(r);
    t.config_index = ocudu_rd_u8(r);
    t.sequence_id  = ocudu_rd_u16(r);
    t.bandwidth_index = ocudu_rd_u8(r);
    t.comb_size  = ocudu_rd_u8(r);
    t.comb_offset = ocudu_rd_u8(r);
    t.cyclic_shift = ocudu_rd_u8(r);
    t.freq_position = ocudu_rd_u8(r);
    t.freq_shift = ocudu_rd_u16(r);
    t.freq_hopping = ocudu_rd_u8(r);
    t.grp_seq_hopping = ocudu_rd_u8(r);
    t.resource_type = ocudu_rd_u8(r);
    t.t_srs      = ocudu_rd_u16(r);
    t.t_offset   = ocudu_rd_u16(r);
    ocudu_rd_u8(r); ocudu_rd_u8(r);       // iq_matrix_report, positioning_report
    if (o) *o = t;
}

// No-beamforming: num_prgs=0 -> OAI takes no precoding-matrix path on UL RX.
static void ul_bf_none(nfapi_nr_ul_beamforming_t* bf)
{
    bf->trp_scheme = 0;
    bf->num_prgs = 0;
    bf->prg_size = 0;
    bf->dig_bf_interface = 0;
}

// LDPC base graph derive (TS38.212 5.3.2) when OCUDU leaves it 0 (its PHY
// derives it internally; OAI indexes index_k0[BG-1] and would read [-1]).
static uint8_t derive_bg(uint8_t ocudu_bg, uint32_t tb_size, uint16_t tcr)
{
    if (ocudu_bg == 1 || ocudu_bg == 2) return ocudu_bg;
    double A = (double)tb_size * 8.0;
    double R = (tcr > 0) ? (double)tcr / 10240.0 : 0.5;
    return (A <= 292.0 || (R <= 0.67 && A <= 3824.0) || R <= 0.25) ? 2 : 1;
}

static void map_prach(nfapi_nr_ul_tti_request_number_of_pdus_t* p,
                      const cap_prach_t* c, uint16_t pci)
{
    p->pdu_type = NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE;
    p->pdu_size = (uint16_t)sizeof(nfapi_nr_prach_pdu_t);
    nfapi_nr_prach_pdu_t* o = &p->prach_pdu;
    o->phys_cell_id       = pci;
    o->num_prach_ocas     = c->num_prach_ocas;
    o->prach_format       = c->prach_format;
    o->num_ra             = c->index_fd_ra;
    o->prach_start_symbol = c->prach_start_symbol;
    o->num_cs             = c->num_cs;
    ul_bf_none(&o->beamforming);
}

static void map_pusch(nfapi_nr_ul_tti_request_number_of_pdus_t* p, const cap_pusch_t* c)
{
    p->pdu_type = NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE;
    p->pdu_size = (uint16_t)sizeof(nfapi_nr_pusch_pdu_t);
    nfapi_nr_pusch_pdu_t* o = &p->pusch_pdu;
    o->pdu_bit_map   = (uint16_t)c->pdu_bitmap;
    o->rnti          = c->rnti;
    o->handle        = c->handle;
    o->bwp_size      = (uint16_t)(c->bwp_stop - c->bwp_start);
    o->bwp_start     = (uint16_t)c->bwp_start;
    o->subcarrier_spacing = c->scs;
    o->cyclic_prefix = c->cp;
    o->target_code_rate = c->target_code_rate;
    o->qam_mod_order = c->qam;
    o->mcs_index     = c->mcs;
    o->mcs_table     = c->mcs_table;
    // Transform-precoding polarity is INVERTED: OCUDU bool (1=enabled DFT-s-OFDM)
    // vs nFAPI/OAI enum (0=enabled, 1=disabled). Must invert.
    o->transform_precoding = c->transform_precoding ? 0 /* enabled */ : 1 /* disabled */;
    o->data_scrambling_id = c->nid_pusch;
    o->nrOfLayers    = c->num_layers ? c->num_layers : 1;
    o->ul_dmrs_symb_pos = c->ul_dmrs_symb_pos;
    o->dmrs_config_type = c->dmrs_type;
    o->ul_dmrs_scrambling_id = c->dmrs_scram_id;
    o->pusch_identity = c->dmrs_identity;
    o->scid          = c->nscid;
    o->num_dmrs_cdm_grps_no_data = c->num_cdm_no_data;
    o->dmrs_ports    = c->dmrs_ports;
    o->resource_alloc = c->resource_alloc;
    memcpy(o->rb_bitmap, c->rb_bitmap, OCUDU_PUSCH_RB_BITMAP_BYTES);
    o->rb_start      = (uint16_t)c->vrb_start;
    o->rb_size       = (uint16_t)(c->vrb_stop - c->vrb_start);
    o->vrb_to_prb_mapping = c->vrb_to_prb;
    o->frequency_hopping  = c->intra_hop;
    o->tx_direct_current_location = c->tx_dc;
    o->uplink_frequency_shift_7p5khz = c->ufs_7p5;
    o->start_symbol_index = c->sym_start;
    o->nr_of_symbols = (uint8_t)(c->sym_stop - c->sym_start);
    o->pusch_data.rv_index           = c->rv_index;
    o->pusch_data.harq_process_id    = c->harq_id;
    o->pusch_data.new_data_indicator = c->new_data;
    o->pusch_data.tb_size            = c->tb_size;
    o->pusch_data.num_cb             = c->num_cb;
    o->pusch_uci.harq_ack_bit_length  = c->harq_ack_bits;
    o->pusch_uci.csi_part1_bit_length = c->csi1_bits;
    o->pusch_uci.csi_part2_bit_length = c->csi2_flags;
    o->pusch_uci.alpha_scaling        = c->alpha;
    o->pusch_uci.beta_offset_harq_ack = c->beta_harq;
    o->pusch_uci.beta_offset_csi1     = c->beta_csi1;
    o->pusch_uci.beta_offset_csi2     = c->beta_csi2;
    o->pusch_ptrs.num_ptrs_ports = 0;
    o->pusch_ptrs.ptrs_ports_list = NULL;
    // Forward OCUDU's low-PAPR u/v (0/0) verbatim; matches the known-good native
    // OAI reference. Do NOT recompute u/v here.
    o->dfts_ofdm.low_papr_group_number    = c->dfts_grp;
    o->dfts_ofdm.low_papr_sequence_number = c->dfts_seq;
    o->dfts_ofdm.ul_ptrs_sample_density   = c->dfts_sample_den;
    o->dfts_ofdm.ul_ptrs_time_density_transform_precoding = c->dfts_time_den;
    ul_bf_none(&o->beamforming);
    o->maintenance_parms_v3.ldpcBaseGraph =
        derive_bg(c->ldpc_base_graph, c->tb_size, c->target_code_rate);
    o->maintenance_parms_v3.tbSizeLbrmBytes = c->tb_size_lbrm;
}

static void map_pucch(nfapi_nr_ul_tti_request_number_of_pdus_t* p, const cap_pucch_t* c)
{
    p->pdu_type = NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE;
    p->pdu_size = (uint16_t)sizeof(nfapi_nr_pucch_pdu_t);
    nfapi_nr_pucch_pdu_t* o = &p->pucch_pdu;
    o->rnti          = c->rnti;
    o->handle        = c->handle;
    o->bwp_size      = (uint16_t)(c->bwp_stop - c->bwp_start);
    o->bwp_start     = (uint16_t)c->bwp_start;
    o->subcarrier_spacing = c->scs;
    o->cyclic_prefix = c->cp;
    o->format_type   = c->format_type;
    o->multi_slot_tx_indicator = c->multi_slot;
    o->pi_2bpsk      = c->pi2_bpsk;
    o->prb_start     = (uint16_t)c->prb_start;
    o->prb_size      = (uint16_t)(c->prb_stop - c->prb_start);
    o->start_symbol_index = c->sym_start;
    o->nr_of_symbols = (uint8_t)(c->sym_stop - c->sym_start);
    o->freq_hop_flag = c->intra_hop;
    o->second_hop_prb = c->second_hop_prb;
    o->group_hop_flag    = (c->grp_hopping == 1) ? 1 : 0;  // ENABLE = group hopping
    o->sequence_hop_flag = (c->grp_hopping == 2) ? 1 : 0;  // DISABLE = sequence hopping
    o->hopping_id        = c->nid_hopping;
    o->initial_cyclic_shift = c->init_cyclic_shift;
    o->data_scrambling_id = c->nid_scrambling;
    o->time_domain_occ_idx = c->time_occ;
    o->pre_dft_occ_idx = c->pre_dft_idx;
    o->pre_dft_occ_len = c->pre_dft_len;
    o->add_dmrs_flag = c->add_dmrs;
    o->dmrs_scrambling_id = c->nid0_dmrs;
    o->dmrs_cyclic_shift = c->m0_shift;
    o->sr_flag       = c->sr_bit_len;
    o->bit_len_harq  = c->bit_len_harq;
    o->bit_len_csi_part1 = c->csi1_bits;
    o->bit_len_csi_part2 = 0;
    ul_bf_none(&o->beamforming);
}

static void map_srs(nfapi_nr_ul_tti_request_number_of_pdus_t* p, const cap_srs_t* c)
{
    p->pdu_type = NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE;
    p->pdu_size = (uint16_t)sizeof(nfapi_nr_srs_pdu_t);
    nfapi_nr_srs_pdu_t* o = &p->srs_pdu;
    o->rnti          = c->rnti;
    o->handle        = c->handle;
    o->bwp_size      = (uint16_t)(c->bwp_stop - c->bwp_start);
    o->bwp_start     = (uint16_t)c->bwp_start;
    o->subcarrier_spacing = c->scs;
    o->cyclic_prefix = c->cp;
    o->num_ant_ports = c->num_ant_ports;
    o->num_symbols   = (uint8_t)(c->sym_stop - c->sym_start);
    o->num_repetitions = c->num_repetitions;
    o->time_start_position = c->time_start;
    o->config_index  = c->config_index;
    o->sequence_id   = c->sequence_id;
    o->bandwidth_index = c->bandwidth_index;
    o->comb_size     = c->comb_size;
    o->comb_offset   = c->comb_offset;
    o->cyclic_shift  = c->cyclic_shift;
    o->frequency_position = c->freq_position;
    o->frequency_shift = c->freq_shift;
    o->frequency_hopping = c->freq_hopping;
    o->group_or_sequence_hopping = c->grp_seq_hopping;
    o->resource_type = c->resource_type;
    o->t_srs         = c->t_srs;
    o->t_offset      = c->t_offset;
    ul_bf_none(&o->beamforming);
}

int aerial_l2l1_ul_tti_request(struct AppContext* ctx,
                              const uint8_t* body, uint32_t body_len)
{
    ocudu_rd_t r;
    ocudu_rd_init(&r, body, body_len);

    uint8_t valid = ocudu_rd_u8(&r);
    if (!valid) {
        SM_Logs(LOG_WARN, _P7_, "[L2->L1 P7] UL_TTI invalid slot_point; dropping.");
        return 0;
    }
    uint8_t  mu    = ocudu_rd_u8(&r);
    uint32_t count = ocudu_rd_u32(&r);
    uint16_t per_type[OCUDU_UL_TTI_NUM_PDU_TYPES];
    for (unsigned i = 0; i < OCUDU_UL_TTI_NUM_PDU_TYPES; ++i) per_type[i] = ocudu_rd_u16(&r);
    uint16_t num_groups = ocudu_rd_u16(&r);
    (void)num_groups; (void)per_type;
    uint16_t total_pdus = ocudu_rd_u16(&r);
    if (r.error) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] UL_TTI header underrun; dropping.");
        return -1;
    }

    unsigned slots_per_frame = 10u << mu;
    uint16_t sfn  = (uint16_t)((count / slots_per_frame) % 1024u);
    uint16_t slot = (uint16_t)(count % slots_per_frame);

    cap_prach_t prach[NFAPI_MAX_NUM_UL_PDU]; unsigned n_prach = 0;
    cap_pusch_t pusch[NFAPI_MAX_NUM_UL_PDU]; unsigned n_pusch = 0;
    cap_pucch_t pucch[NFAPI_MAX_NUM_UL_PDU]; unsigned n_pucch = 0;
    cap_srs_t   srs[NFAPI_MAX_NUM_UL_PDU];   unsigned n_srs = 0;

    for (uint16_t i = 0; i < total_pdus && !r.error; ++i) {
        uint16_t pdu_type = ocudu_rd_u16(&r);
        ocudu_rd_u16(&r);                       // pdu_size (recomputed by us)
        cap_prach_t pr; cap_pusch_t pu; cap_pucch_t pc; cap_srs_t sr;
        read_prach(&r, &pr);
        walk_pusch(&r, &pu);
        walk_pucch(&r, &pc);
        walk_srs(&r, &sr);
        if (r.error) break;
        switch (pdu_type) {
            case OCUDU_UL_PDU_TYPE_PRACH:
                if (n_prach < NFAPI_MAX_NUM_UL_PDU) { prach[n_prach++] = pr; }
                break;
            case OCUDU_UL_PDU_TYPE_PUSCH:
            case OCUDU_UL_PDU_TYPE_MSGA_PUSCH:
                if (n_pusch < NFAPI_MAX_NUM_UL_PDU) { pusch[n_pusch++] = pu; }
                break;
            case OCUDU_UL_PDU_TYPE_PUCCH_F01:
            case OCUDU_UL_PDU_TYPE_PUCCH_F234:
                if (n_pucch < NFAPI_MAX_NUM_UL_PDU) { pucch[n_pucch++] = pc; }
                break;
            case OCUDU_UL_PDU_TYPE_SRS:
                if (n_srs < NFAPI_MAX_NUM_UL_PDU) { srs[n_srs++] = sr; }
                break;
            default: break;
        }
    }

    int walk_ok = (!r.error && r.off == body_len);
    if (total_pdus > 0 && !walk_ok) {
        SM_Logs(LOG_WARN, _P7_,
                "[L2->L1 P7] UL_TTI slot=%u.%u PDU walk desync "
                "(consumed=%u body=%u err=%d total=%u) -> dropping.",
                sfn, slot, r.off, body_len, r.error, total_pdus);
        return -1;
    }

    unsigned total = n_prach + n_pusch + n_pucch + n_srs;
    if (total == 0) {
        return 0;   // empty UL_TTI carries no uplink config
    }

    nfapi_nr_ul_tti_request_t* ul =
        (nfapi_nr_ul_tti_request_t*)calloc(1, sizeof(*ul));
    if (ul == NULL) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] UL_TTI calloc failed.");
        return -1;
    }
    ul->header.phy_id     = 0;
    ul->header.message_id = NFAPI_NR_PHY_MSG_TYPE_UL_TTI_REQUEST;
    ul->SFN  = sfn;
    ul->Slot = slot;

    unsigned n = 0;
    for (unsigned i = 0; i < n_prach && n < NFAPI_MAX_NUM_UL_PDU; ++i) {
        map_prach(&ul->pdus_list[n++], &prach[i], (uint16_t)ctx->aerial_ocudu_ctx.cell_pci);
    }
    for (unsigned i = 0; i < n_pusch && n < NFAPI_MAX_NUM_UL_PDU; ++i) {
        map_pusch(&ul->pdus_list[n++], &pusch[i]);
    }
    for (unsigned i = 0; i < n_pucch && n < NFAPI_MAX_NUM_UL_PDU; ++i) {
        map_pucch(&ul->pdus_list[n++], &pucch[i]);
    }
    for (unsigned i = 0; i < n_srs && n < NFAPI_MAX_NUM_UL_PDU; ++i) {
        map_srs(&ul->pdus_list[n++], &srs[i]);
    }

    ul->n_pdus       = (uint8_t)n;
    ul->rach_present = (n_prach > 0) ? 1 : 0;
    ul->n_ulsch      = (uint8_t)n_pusch;
    ul->n_ulcch      = (uint8_t)n_pucch;
    ul->n_group      = 0;

    // Record the translated nFAPI message for the XFAPI dashboard.
    {
        char content[MAX_MESSAGE_CONTENT_LEN];
        content[0] = '\0';
        serialize_nfapi_ul_tti_request_message(content, sizeof(content), ul);
        add_message_stats("UL_TTI_REQUEST", ul->SFN, ul->Slot,
                          (int)sizeof(*ul), ul->n_pdus, content, 0);
    }

    int rc = aerial_send_p7_msg(ctx, (nfapi_p7_message_header_t*)&ul->header);
    free(ul);
    return (rc == 0) ? 1 : -1;
}

#endif /* AERIAL_OCUDU */
