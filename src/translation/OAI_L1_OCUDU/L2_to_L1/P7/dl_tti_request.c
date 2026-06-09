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

// OAI_OCUDU P7 L2->L1: DL_TTI.request (OCUDU-FAPI -> nFAPI -> PNF).

#include "oai_l2_to_l1_p7.h"

#ifdef OAI_OCUDU

#include <stdlib.h>
#include <string.h>

#include "ocudu_fapi_wire.h"
#include "oai_vnf.h"
#include "unified_logger.h"
#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"
#include "message_stats.h"

#define OCUDU_DL_TTI_NUM_PDU_TYPES 6
#define OCUDU_DL_PDU_TYPE_PDCCH    0
#define OCUDU_DL_PDU_TYPE_PDSCH    1
#define OCUDU_DL_PDU_TYPE_CSI_RS   2
#define OCUDU_DL_PDU_TYPE_SSB      3
#define OCUDU_PDSCH_RB_BITMAP_BYTES 36

// ---- capture structs (only the fields we map to nFAPI) ----

typedef struct {
    uint16_t pci, beta, blk_idx, subc_off, offset_pointA;
    uint32_t bch;
    uint8_t  case_type, scs, l_max;
} cap_ssb_t;

typedef struct {
    uint32_t bwp_start, bwp_stop;   // coreset_bwp (half-open)
    uint8_t  scs, cp;
    uint8_t  sym_start, sym_stop;   // symbols (half-open)
    uint8_t  freq_dom[6];           // freq_domain_resource (45-bit bitmap)
    uint8_t  mapping_idx, reg_bundle, interleaver;
    uint16_t shift_index;
    uint8_t  precoder_gran;
    // single DCI
    uint16_t dci_rnti, nid_data, nid_dmrs, nrnti;
    uint8_t  cce, agg_enum;
    uint16_t prg_size, pm_index;
    int8_t   power_ss;
    uint8_t  dci_payload[DCI_PAYLOAD_BYTE_LEN];
    uint32_t dci_nbits;
} cap_pdcch_t;

typedef struct {
    uint64_t pdu_bitmap;
    uint16_t rnti, pdu_index;
    uint32_t bwp_start, bwp_stop;
    uint8_t  scs, cp;
    uint8_t  nof_cws;
    uint16_t target_code_rate[2];
    uint8_t  qam[2], mcs[2], mcs_table[2], rv[2];
    uint32_t tb_size[2];
    uint16_t nid_pdsch;
    uint8_t  num_layers, tx_scheme, ref_point;
    uint16_t dl_dmrs_symb_pos, dmrs_scram_id;
    uint8_t  dmrs_type;
    uint16_t dmrs_scram_id_compl;
    uint8_t  low_papr, nscid, num_cdm_no_data;
    uint16_t dmrs_ports;
    uint8_t  resource_alloc;
    uint8_t  rb_bitmap[OCUDU_PDSCH_RB_BITMAP_BYTES];
    uint32_t vrb_start, vrb_stop;
    uint8_t  vrb_to_prb;
    uint8_t  sym_start, sym_stop;
    int32_t  power_nr;
    uint8_t  power_ss;
    uint16_t prg_size, pm_index;
    uint8_t  ldpc_base_graph;
    uint32_t tb_size_lbrm;
} cap_pdsch_t;

typedef struct {
    uint8_t  scs, cp;
    uint32_t crb_start, crb_stop;
    uint8_t  type, row;
    uint8_t  freq_dom[2];           // bounded_bitset<12>
    uint8_t  symb_L0, symb_L1, cdm_type, freq_density;
    uint16_t scramb_id;
    int32_t  power_nr;
    uint8_t  power_ss;
    uint32_t bwp_start, bwp_stop;
} cap_csi_t;

// Read a bounded_bitset into dst (LSB-first byte packing): logical bit i ->
// dst[i/8] bit (i&7). Returns nbits. Always advances the reader the full
// serialized length (u32 nbits + ceil(nbits/64) u64 chunks).
static uint32_t rd_bitset_bytes(ocudu_rd_t* r, uint8_t* dst, uint32_t dst_cap)
{
    uint32_t nbits  = ocudu_rd_u32(r);
    if (dst) memset(dst, 0, dst_cap);
    uint32_t chunks = (nbits + 63u) / 64u;
    for (uint32_t c = 0; c < chunks; ++c) {
        uint64_t v = ocudu_rd_u64(r);
        if (!dst) continue;
        for (uint32_t b = 0; b < 64u; ++b) {
            uint32_t bit = c * 64u + b;
            if (bit >= nbits) break;
            if ((v >> b) & 1ull) {
                uint32_t byte = bit >> 3;
                if (byte < dst_cap) dst[byte] |= (uint8_t)(1u << (bit & 7u));
            }
        }
    }
    return nbits;
}

// ---- capture walkers (mirror fapi_serial::deserialize, advance + capture) ----

static void walk_dci(ocudu_rd_t* r, cap_pdcch_t* o)
{
    uint16_t rnti = ocudu_rd_u16(r);
    uint16_t nidd = ocudu_rd_u16(r);
    uint16_t nidm = ocudu_rd_u16(r);
    uint16_t nrnti = ocudu_rd_u16(r);
    uint8_t  cce  = ocudu_rd_u8(r);
    uint8_t  agg  = ocudu_rd_u8(r);
    uint16_t prg  = ocudu_rd_u16(r);
    uint16_t pmi  = ocudu_rd_u16(r);
    uint8_t  power_idx = ocudu_rd_u8(r);
    int8_t   pss = 0;
    if (power_idx == 0) {
        pss = ocudu_rd_i8(r);
    } else {
        ocudu_rd_u32(r); ocudu_rd_u32(r);
    }
    uint8_t  payload[DCI_PAYLOAD_BYTE_LEN];
    uint32_t nbits = rd_bitset_bytes(r, payload, sizeof(payload));
    uint8_t has_ctx = ocudu_rd_u8(r);
    if (has_ctx) {
        ocudu_rd_u8(r);
        uint16_t fmt_len = ocudu_rd_u16(r);
        ocudu_rd_skip(r, fmt_len);
        uint8_t has_hft = ocudu_rd_u8(r);
        if (has_hft) ocudu_rd_u32(r);
    }
    if (o) {
        o->dci_rnti = rnti; o->nid_data = nidd; o->nid_dmrs = nidm;
        o->nrnti = nrnti; o->cce = cce; o->agg_enum = agg;
        o->prg_size = prg; o->pm_index = pmi; o->power_ss = pss;
        o->dci_nbits = nbits;
        memcpy(o->dci_payload, payload, sizeof(payload));
    }
}

static void walk_pdcch(ocudu_rd_t* r, cap_pdcch_t* o)
{
    uint32_t bs = ocudu_rd_u32(r), bp = ocudu_rd_u32(r);  // coreset_bwp [start,stop)
    uint8_t scs = ocudu_rd_u8(r), cp = ocudu_rd_u8(r);
    uint8_t ss = ocudu_rd_u8(r), sp = ocudu_rd_u8(r);     // symbols [start,stop)
    uint8_t freq[6];
    rd_bitset_bytes(r, freq, sizeof(freq));               // freq_domain_resource
    uint8_t mi = ocudu_rd_u8(r);
    uint8_t rb = 0, il = 0; uint16_t sh = 0;
    if (mi == 0 || mi == 1) { rb = ocudu_rd_u8(r); il = ocudu_rd_u8(r); sh = ocudu_rd_u16(r); }
    else                    { rb = ocudu_rd_u8(r); }
    uint8_t pg = ocudu_rd_u8(r);
    if (o) {
        o->bwp_start = bs; o->bwp_stop = bp; o->scs = scs; o->cp = cp;
        o->sym_start = ss; o->sym_stop = sp;
        memcpy(o->freq_dom, freq, sizeof(freq));
        o->mapping_idx = mi; o->reg_bundle = rb; o->interleaver = il;
        o->shift_index = sh; o->precoder_gran = pg;
    }
    walk_dci(r, o);
}

static void walk_pdsch_maint_v3(ocudu_rd_t* r, cap_pdsch_t* o)
{
    ocudu_rd_u8(r);                       // trans_type
    ocudu_rd_u16(r);                      // coreset_start_point
    ocudu_rd_u16(r);                      // initial_dl_bwp_size
    uint8_t ldpc = ocudu_rd_u8(r);        // ldpc_base_graph
    uint32_t lbrm = ocudu_rd_u32(r);      // tb_size_lbrm_bytes
    ocudu_rd_u8(r);                       // tb_crc_required
    for (int i = 0; i < 8; ++i) ocudu_rd_u16(r);
    ocudu_rd_u16(r);                      // ssb_config_for_rate_matching
    ocudu_rd_u8(r);
    ocudu_rd_skip_sv_u8(r);
    ocudu_rd_u8(r); ocudu_rd_u8(r);
    ocudu_rd_u16(r); ocudu_rd_u16(r);
    ocudu_rd_u8(r);
    ocudu_rd_skip_sv_u8(r);
    ocudu_rd_skip_sv_u16(r);
    ocudu_rd_u8(r);
    ocudu_rd_skip_sv_u8(r);
    if (o) { o->ldpc_base_graph = ldpc; o->tb_size_lbrm = lbrm; }
}

static void walk_pdsch(ocudu_rd_t* r, cap_pdsch_t* o)
{
    uint64_t bm = ocudu_rd_u64(r);
    uint16_t rnti = ocudu_rd_u16(r);
    uint16_t pidx = ocudu_rd_u16(r);
    uint32_t bs = ocudu_rd_u32(r), bp = ocudu_rd_u32(r);
    uint8_t scs = ocudu_rd_u8(r), cp = ocudu_rd_u8(r);
    uint16_t nof_cws = ocudu_rd_u16(r);
    if (o) { o->pdu_bitmap = bm; o->rnti = rnti; o->pdu_index = pidx;
             o->bwp_start = bs; o->bwp_stop = bp; o->scs = scs; o->cp = cp;
             o->nof_cws = (uint8_t)(nof_cws > 2 ? 2 : nof_cws); }
    for (uint16_t i = 0; i < nof_cws; ++i) {
        uint16_t tcr = ocudu_rd_u16(r);
        uint8_t qam = ocudu_rd_u8(r), mcs = ocudu_rd_u8(r);
        uint8_t mtb = ocudu_rd_u8(r), rv = ocudu_rd_u8(r);
        uint32_t tbs = ocudu_rd_u32(r);
        if (o && i < 2) { o->target_code_rate[i] = tcr; o->qam[i] = qam;
                          o->mcs[i] = mcs; o->mcs_table[i] = mtb; o->rv[i] = rv;
                          o->tb_size[i] = tbs; }
    }
    uint16_t nid = ocudu_rd_u16(r);
    uint8_t nl = ocudu_rd_u8(r), ts = ocudu_rd_u8(r), rp = ocudu_rd_u8(r);
    uint16_t dmrs_pos = ocudu_rd_u16(r);
    uint16_t dmrs_sid = ocudu_rd_u16(r);
    uint8_t dmrs_type = ocudu_rd_u8(r);
    uint16_t dmrs_sid_c = ocudu_rd_u16(r);
    uint8_t low_papr = ocudu_rd_u8(r);
    uint8_t nscid = ocudu_rd_u8(r);
    uint8_t ncdm = ocudu_rd_u8(r);
    uint16_t dports = ocudu_rd_u16(r);
    uint8_t ralloc = ocudu_rd_u8(r);
    uint8_t rbbm[OCUDU_PDSCH_RB_BITMAP_BYTES];
    ocudu_rd_bytes(r, rbbm, sizeof(rbbm));
    uint32_t vs = ocudu_rd_u32(r), vp = ocudu_rd_u32(r);
    uint8_t v2p = ocudu_rd_u8(r);
    uint8_t ssy = ocudu_rd_u8(r), spy = ocudu_rd_u8(r);
    uint8_t power_idx = ocudu_rd_u8(r);
    int32_t pnr = 0; uint8_t pss = 0;
    if (power_idx == 0) { pnr = ocudu_rd_i32(r); pss = ocudu_rd_u8(r); }
    else                { ocudu_rd_u32(r); ocudu_rd_u32(r); }
    uint16_t prg = ocudu_rd_u16(r), pmi = ocudu_rd_u16(r);
    ocudu_rd_u8(r);                       // is_last_cb_present
    ocudu_rd_u8(r);                       // is_inline_tb_crc
    ocudu_rd_u32(r); ocudu_rd_u32(r);     // dl_tb_crc_cw[2]
    walk_pdsch_maint_v3(r, o);
    ocudu_rd_u8(r); ocudu_rd_skip_sv_u8(r);   // params_v4
    ocudu_rd_u8(r); ocudu_rd_skip_sv_u8(r);
    uint8_t has_ctx = ocudu_rd_u8(r);
    if (has_ctx) { ocudu_rd_u8(r); ocudu_rd_u32(r); ocudu_rd_u32(r); }
    if (o) {
        o->nid_pdsch = nid; o->num_layers = nl; o->tx_scheme = ts; o->ref_point = rp;
        o->dl_dmrs_symb_pos = dmrs_pos; o->dmrs_scram_id = dmrs_sid;
        o->dmrs_type = dmrs_type; o->dmrs_scram_id_compl = dmrs_sid_c;
        o->low_papr = low_papr; o->nscid = nscid; o->num_cdm_no_data = ncdm;
        o->dmrs_ports = dports; o->resource_alloc = ralloc;
        memcpy(o->rb_bitmap, rbbm, sizeof(rbbm));
        o->vrb_start = vs; o->vrb_stop = vp; o->vrb_to_prb = v2p;
        o->sym_start = ssy; o->sym_stop = spy;
        o->power_nr = pnr; o->power_ss = pss; o->prg_size = prg; o->pm_index = pmi;
    }
}

static void walk_csi_rs(ocudu_rd_t* r, cap_csi_t* o)
{
    uint8_t scs = ocudu_rd_u8(r), cp = ocudu_rd_u8(r);
    uint32_t cs = ocudu_rd_u32(r), cp2 = ocudu_rd_u32(r);   // crbs
    uint8_t type = ocudu_rd_u8(r), row = ocudu_rd_u8(r);
    uint8_t freq[2];
    rd_bitset_bytes(r, freq, sizeof(freq));                 // freq_domain<12>
    uint8_t l0 = ocudu_rd_u8(r), l1 = ocudu_rd_u8(r);
    uint8_t cdm = ocudu_rd_u8(r), den = ocudu_rd_u8(r);
    uint16_t scr = ocudu_rd_u16(r);
    int32_t pnr = ocudu_rd_i32(r);
    uint8_t pss = ocudu_rd_u8(r);
    uint32_t bs = ocudu_rd_u32(r), bp = ocudu_rd_u32(r);    // bwp
    if (o) {
        o->scs = scs; o->cp = cp; o->crb_start = cs; o->crb_stop = cp2;
        o->type = type; o->row = row; memcpy(o->freq_dom, freq, sizeof(freq));
        o->symb_L0 = l0; o->symb_L1 = l1; o->cdm_type = cdm; o->freq_density = den;
        o->scramb_id = scr; o->power_nr = pnr; o->power_ss = pss;
        o->bwp_start = bs; o->bwp_stop = bp;
    }
}

static void read_ssb(ocudu_rd_t* r, cap_ssb_t* s)
{
    cap_ssb_t t;
    t.pci           = ocudu_rd_u16(r);
    t.beta          = ocudu_rd_u8(r);
    t.blk_idx       = ocudu_rd_u8(r);
    t.subc_off      = ocudu_rd_u16(r);
    t.offset_pointA = ocudu_rd_u16(r);
    t.bch           = ocudu_rd_u32(r);
    t.case_type     = ocudu_rd_u8(r);
    t.scs           = ocudu_rd_u8(r);
    t.l_max         = ocudu_rd_u8(r);
    if (s) *s = t;
}

static void walk_prs(ocudu_rd_t* r)
{
    ocudu_rd_u8(r); ocudu_rd_u8(r); ocudu_rd_u16(r);
    ocudu_rd_u8(r); ocudu_rd_u8(r); ocudu_rd_u8(r); ocudu_rd_u8(r);
    ocudu_rd_u32(r); ocudu_rd_u32(r);                       // crbs
    uint8_t has_power = ocudu_rd_u8(r);
    if (has_power) ocudu_rd_u32(r);
    ocudu_rd_u16(r); ocudu_rd_u16(r);
}

// ---- nFAPI mappers ----

// No-precoding beamforming: one PRG over the whole allocation with pm_idx=0
// (unitary). pm_idx must stay 0 (non-zero indexes an absent PMI matrix in OAI).
static void map_beamforming_none(nfapi_nr_tx_precoding_and_beamforming_t* pb)
{
    pb->num_prgs = 1;
    pb->prg_size = 275;          // >= any BWP size -> all RBs in PRG 0
    pb->dig_bf_interfaces = 0;
    pb->prgs_list[0].pm_idx = 0; // unitary precoding (no matrix lookup)
}

static void map_ssb(nfapi_nr_dl_tti_request_pdu_t* p, const cap_ssb_t* s)
{
    p->PDUType = NFAPI_NR_DL_TTI_SSB_PDU_TYPE;
    p->PDUSize = (uint32_t)sizeof(nfapi_nr_dl_tti_ssb_pdu);
    nfapi_nr_dl_tti_ssb_pdu_rel15_t* o = &p->ssb_pdu.ssb_pdu_rel15;
    o->PhysCellId = s->pci;
    o->BetaPss = (uint8_t)s->beta;
    o->SsbBlockIndex = (uint8_t)s->blk_idx;
    o->SsbSubcarrierOffset = (uint8_t)s->subc_off;
    o->ssbOffsetPointA = s->offset_pointA;
    o->bchPayloadFlag = 0;
    // MIB byte-order conversion: OCUDU packs the 24-bit MIB MSB-first; OAI reads
    // bchPayload as a byte array (byte 0 first), so byte-reverse the 3 MIB bytes.
    {
        uint32_t v = s->bch & 0xffffffu;
        o->bchPayload = ((v & 0x0000ffu) << 16) | (v & 0x00ff00u) | ((v >> 16) & 0xffu);
    }
}

static void fill_pdcch_rel15(nfapi_nr_dl_tti_pdcch_pdu_rel15_t* o, const cap_pdcch_t* c)
{
    o->BWPSize  = (uint16_t)(c->bwp_stop - c->bwp_start);
    o->BWPStart = (uint16_t)c->bwp_start;
    o->SubcarrierSpacing = c->scs;
    o->CyclicPrefix = c->cp;
    o->StartSymbolIndex = c->sym_start;
    o->DurationSymbols = (uint8_t)(c->sym_stop - c->sym_start);
    memcpy(o->FreqDomainResource, c->freq_dom, 6);
    // OCUDU mapping idx: 0=CORESET0(interleaved), 1=interleaved, 2=non-interleaved
    // -> nFAPI CceRegMappingType (1=interleaved, 0=non-interleaved).
    o->CceRegMappingType = (c->mapping_idx == 2) ? 0 : 1;
    o->RegBundleSize = c->reg_bundle;
    o->InterleaverSize = c->interleaver;
    o->CoreSetType = (c->mapping_idx == 0) ? 0 : 1;        // coreset0(PBCH/SIB1) vs other
    o->ShiftIndex = c->shift_index;
    o->precoderGranularity = c->precoder_gran;
    o->numDlDci = 1;
    nfapi_nr_dl_dci_pdu_t* d = &o->dci_pdu[0];
    d->RNTI = c->dci_rnti;
    d->ScramblingId = c->nid_data;
    d->ScramblingRNTI = c->nrnti;
    d->CceIndex = c->cce;
    d->AggregationLevel = (uint8_t)(1u << (c->agg_enum & 0x7));  // n1..n16 -> 1..16
    map_beamforming_none(&d->precodingAndBeamforming);
    d->beta_PDCCH_1_0 = 0;
    d->powerControlOffsetSS = (uint8_t)c->power_ss;
    d->PayloadSizeBits = (uint16_t)c->dci_nbits;
    // DCI bit-order: OCUDU stores transmitted bit k LSB-first; OAI reads Payload
    // as little-endian uint64 with transmitted bit k at uint64 bit (nbits-1-k).
    memset(d->Payload, 0, sizeof(d->Payload));
    {
        const uint32_t nbits = (c->dci_nbits <= 64u) ? c->dci_nbits : 64u;
        uint64_t u = 0;
        for (uint32_t k = 0; k < nbits; ++k) {
            if ((c->dci_payload[k >> 3] >> (k & 7u)) & 1u) {
                u |= (uint64_t)1u << (nbits - 1u - k);
            }
        }
        for (uint32_t b = 0; b < 8u && b < DCI_PAYLOAD_BYTE_LEN; ++b) {
            d->Payload[b] = (uint8_t)((u >> (8u * b)) & 0xffu);   // little-endian
        }
    }
}

// Wrap fill_pdcch_rel15 into an nFAPI DL_TTI PDCCH PDU (used by DL_TTI for DL DCIs).
static void map_pdcch(nfapi_nr_dl_tti_request_pdu_t* p, const cap_pdcch_t* c)
{
    p->PDUType = NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE;
    p->PDUSize = (uint32_t)sizeof(nfapi_nr_dl_tti_pdcch_pdu);
    fill_pdcch_rel15(&p->pdcch_pdu.pdcch_pdu_rel15, c);
}

static void map_pdsch(nfapi_nr_dl_tti_request_pdu_t* p, const cap_pdsch_t* c)
{
    p->PDUType = NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE;
    p->PDUSize = (uint32_t)sizeof(nfapi_nr_dl_tti_pdsch_pdu);
    nfapi_nr_dl_tti_pdsch_pdu_rel15_t* o = &p->pdsch_pdu.pdsch_pdu_rel15;
    o->pduBitmap = (uint16_t)c->pdu_bitmap;
    o->rnti = c->rnti;
    o->pduIndex = c->pdu_index;
    o->BWPSize  = (uint16_t)(c->bwp_stop - c->bwp_start);
    o->BWPStart = (uint16_t)c->bwp_start;
    o->SubcarrierSpacing = c->scs;
    o->CyclicPrefix = c->cp;
    o->NrOfCodewords = c->nof_cws ? c->nof_cws : 1;
    for (int i = 0; i < 2; ++i) {
        o->targetCodeRate[i] = c->target_code_rate[i];
        o->qamModOrder[i] = c->qam[i];
        o->mcsIndex[i] = c->mcs[i];
        o->mcsTable[i] = c->mcs_table[i];
        o->rvIndex[i] = c->rv[i];
        o->TBSize[i] = c->tb_size[i];
    }
    o->dataScramblingId = c->nid_pdsch;
    o->nrOfLayers = c->num_layers ? c->num_layers : 1;
    o->transmissionScheme = c->tx_scheme;
    o->refPoint = c->ref_point;
    o->dlDmrsSymbPos = c->dl_dmrs_symb_pos;
    o->dmrsConfigType = c->dmrs_type;
    o->dlDmrsScramblingId = c->dmrs_scram_id;
    o->SCID = c->nscid;
    o->numDmrsCdmGrpsNoData = c->num_cdm_no_data;
    o->dmrsPorts = c->dmrs_ports;
    o->resourceAlloc = c->resource_alloc;
    memcpy(o->rbBitmap, c->rb_bitmap, OCUDU_PDSCH_RB_BITMAP_BYTES);
    o->rbStart = (uint16_t)c->vrb_start;
    o->rbSize  = (uint16_t)(c->vrb_stop - c->vrb_start);
    o->VRBtoPRBMapping = c->vrb_to_prb;
    o->StartSymbolIndex = c->sym_start;
    o->NrOfSymbols = (uint8_t)(c->sym_stop - c->sym_start);
    map_beamforming_none(&o->precodingAndBeamforming);
    o->powerControlOffset = (uint8_t)c->power_ss;
    o->powerControlOffsetSS = (uint8_t)c->power_ss;
    // OCUDU may leave ldpc_base_graph=0 on the wire; OAI requires BG in {1,2}
    // (indexes index_k0[BG-1]). Derive BG from R and A per TS38.212 5.3.2.
    uint8_t bg = c->ldpc_base_graph;
    if (bg != 1 && bg != 2) {
        double A = (double)c->tb_size[0] * 8.0;                 // TB size in bits
        double R = (c->target_code_rate[0] > 0)
                       ? (double)c->target_code_rate[0] / 10240.0 // FAPI 0.1/1024 units
                       : 0.5;
        bg = (A <= 292.0 || (R <= 0.67 && A <= 3824.0) || R <= 0.25) ? 2 : 1;
    }
    o->maintenance_parms_v3.ldpcBaseGraph = bg;
    o->maintenance_parms_v3.tbSizeLbrmBytes = c->tb_size_lbrm;
}

static void map_csi_rs(nfapi_nr_dl_tti_request_pdu_t* p, const cap_csi_t* c)
{
    p->PDUType = NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE;
    p->PDUSize = (uint32_t)sizeof(nfapi_nr_dl_tti_csi_rs_pdu);
    nfapi_nr_dl_tti_csi_rs_pdu_rel15_t* o = &p->csi_rs_pdu.csi_rs_pdu_rel15;
    o->bwp_size  = (uint16_t)(c->bwp_stop - c->bwp_start);
    o->bwp_start = (uint16_t)c->bwp_start;
    o->subcarrier_spacing = c->scs;
    o->cyclic_prefix = c->cp;
    o->start_rb = (uint16_t)c->crb_start;
    o->nr_of_rbs = (uint16_t)(c->crb_stop - c->crb_start);
    o->csi_type = c->type;
    o->row = c->row;
    o->freq_domain = (uint16_t)(c->freq_dom[0] | (c->freq_dom[1] << 8));
    o->symb_l0 = c->symb_L0;
    o->symb_l1 = c->symb_L1;
    o->cdm_type = c->cdm_type;
    o->freq_density = c->freq_density;
    o->scramb_id = c->scramb_id;
    o->power_control_offset = (uint8_t)c->power_nr;
    o->power_control_offset_ss = c->power_ss;
}

int ocudu_l2l1_dl_tti_request(struct AppContext* ctx, struct oai_vnf* v,
                              const uint8_t* body, uint32_t body_len)
{
    ocudu_rd_t r;
    ocudu_rd_init(&r, body, body_len);

    uint8_t valid = ocudu_rd_u8(&r);
    if (!valid) {
        SM_Logs(LOG_WARN, _P7_, "[L2->L1 P7] DL_TTI invalid slot_point; dropping.");
        return 0;
    }
    uint8_t  mu    = ocudu_rd_u8(&r);
    uint32_t count = ocudu_rd_u32(&r);
    for (unsigned i = 0; i < OCUDU_DL_TTI_NUM_PDU_TYPES; ++i) (void)ocudu_rd_u16(&r);
    uint16_t total_pdus = ocudu_rd_u16(&r);
    if (r.error) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] DL_TTI header underrun; dropping.");
        return -1;
    }

    unsigned slots_per_frame = 10u << mu;
    uint16_t sfn  = (uint16_t)((count / slots_per_frame) % 1024u);
    uint16_t slot = (uint16_t)(count % slots_per_frame);

    cap_ssb_t   ssb[NFAPI_NR_MAX_DL_TTI_PDUS];   unsigned n_ssb = 0;
    cap_pdcch_t pdcch[NFAPI_NR_MAX_DL_TTI_PDUS]; unsigned n_pdcch = 0;
    cap_pdsch_t pdsch[NFAPI_NR_MAX_DL_TTI_PDUS]; unsigned n_pdsch = 0;
    cap_csi_t   csi[NFAPI_NR_MAX_DL_TTI_PDUS];   unsigned n_csi = 0;

    for (uint16_t i = 0; i < total_pdus && !r.error; ++i) {
        uint16_t pdu_type = ocudu_rd_u16(&r);
        cap_pdcch_t pc; cap_pdsch_t ps; cap_csi_t cs; cap_ssb_t sb;
        walk_pdcch(&r, &pc);
        walk_pdsch(&r, &ps);
        walk_csi_rs(&r, &cs);
        read_ssb(&r, &sb);
        walk_prs(&r);
        if (r.error) break;
        switch (pdu_type) {
            case OCUDU_DL_PDU_TYPE_PDCCH: if (n_pdcch < NFAPI_NR_MAX_DL_TTI_PDUS) pdcch[n_pdcch++] = pc; break;
            case OCUDU_DL_PDU_TYPE_PDSCH: if (n_pdsch < NFAPI_NR_MAX_DL_TTI_PDUS) pdsch[n_pdsch++] = ps; break;
            case OCUDU_DL_PDU_TYPE_CSI_RS: if (n_csi  < NFAPI_NR_MAX_DL_TTI_PDUS) csi[n_csi++]   = cs; break;
            case OCUDU_DL_PDU_TYPE_SSB:   if (n_ssb   < NFAPI_NR_MAX_DL_TTI_PDUS) ssb[n_ssb++]   = sb; break;
            default: break;
        }
    }

    int walk_ok = (!r.error && r.off == body_len);
    if (total_pdus > 0 && !walk_ok) {
        SM_Logs(LOG_WARN, _P7_,
                "[L2->L1 P7] DL_TTI slot=%u.%u PDU walk desync "
                "(consumed=%u body=%u err=%d total=%u) -> EMPTY.",
                sfn, slot, r.off, body_len, r.error, total_pdus);
        n_ssb = n_pdcch = n_pdsch = n_csi = 0;
    }

    nfapi_nr_dl_tti_request_t* dl = (nfapi_nr_dl_tti_request_t*)calloc(1, sizeof(*dl));
    if (dl == NULL) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] DL_TTI calloc failed.");
        return -1;
    }
    dl->header.phy_id     = (uint16_t)v->phy_id;
    dl->header.message_id = NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST;
    dl->SFN  = sfn;
    dl->Slot = slot;
    dl->dl_tti_request_body.nGroup = 0;

    unsigned n = 0;
    for (unsigned i = 0; i < n_ssb   && n < NFAPI_NR_MAX_DL_TTI_PDUS; ++i) map_ssb(&dl->dl_tti_request_body.dl_tti_pdu_list[n++], &ssb[i]);
    for (unsigned i = 0; i < n_pdcch && n < NFAPI_NR_MAX_DL_TTI_PDUS; ++i) map_pdcch(&dl->dl_tti_request_body.dl_tti_pdu_list[n++], &pdcch[i]);
    for (unsigned i = 0; i < n_pdsch && n < NFAPI_NR_MAX_DL_TTI_PDUS; ++i) map_pdsch(&dl->dl_tti_request_body.dl_tti_pdu_list[n++], &pdsch[i]);
    for (unsigned i = 0; i < n_csi   && n < NFAPI_NR_MAX_DL_TTI_PDUS; ++i) map_csi_rs(&dl->dl_tti_request_body.dl_tti_pdu_list[n++], &csi[i]);
    dl->dl_tti_request_body.nPDUs = (uint8_t)n;

    // Record the translated nFAPI message for the XFAPI dashboard. message_content
    // carries a per-PDU dump; ipc_latency_ns is left 0 (no L2 TX timestamp on the
    // OCUDU xSM data path yet).
    {
        char content[MAX_MESSAGE_CONTENT_LEN];
        content[0] = '\0';
        serialize_nfapi_dl_tti_request_message(content, sizeof(content), dl);
        add_message_stats("DL_TTI_REQUEST", dl->SFN, dl->Slot,
                          (int)sizeof(*dl), dl->dl_tti_request_body.nPDUs, content, 0);
    }

    int rc = oai_vnf_send_p7(ctx, (nfapi_p7_message_header_t*)&dl->header);
    free(dl);
    return (rc == 0) ? 1 : -1;
}

// OAI_OCUDU P7 L2->L1: UL_DCI.request (OCUDU-FAPI -> nFAPI -> PNF). Carries the
// connected-mode UL grant DCI on the PDCCH, sent as a separate P7 message.
int ocudu_l2l1_ul_dci_request(struct AppContext* ctx, struct oai_vnf* v,
                              const uint8_t* body, uint32_t body_len)
{
    ocudu_rd_t r;
    ocudu_rd_init(&r, body, body_len);

    uint8_t valid = ocudu_rd_u8(&r);
    if (!valid) {
        SM_Logs(LOG_WARN, _P7_, "[L2->L1 P7] UL_DCI invalid slot_point; dropping.");
        return 0;
    }
    uint8_t  mu     = ocudu_rd_u8(&r);
    uint32_t count  = ocudu_rd_u32(&r);
    uint16_t n_pdus = ocudu_rd_u16(&r);
    if (r.error) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] UL_DCI header underrun; dropping.");
        return -1;
    }

    unsigned slots_per_frame = 10u << mu;
    uint16_t sfn  = (uint16_t)((count / slots_per_frame) % 1024u);
    uint16_t slot = (uint16_t)(count % slots_per_frame);

    cap_pdcch_t pdcch[NFAPI_NR_MAX_DL_TTI_PDUS]; unsigned n_pdcch = 0;
    for (uint16_t i = 0; i < n_pdus && !r.error; ++i) {
        cap_pdcch_t pc;
        walk_pdcch(&r, &pc);
        if (r.error) break;
        if (n_pdcch < NFAPI_NR_MAX_DL_TTI_PDUS) pdcch[n_pdcch++] = pc;
    }

    int walk_ok = (!r.error && r.off == body_len);
    if (n_pdus > 0 && !walk_ok) {
        SM_Logs(LOG_WARN, _P7_,
                "[L2->L1 P7] UL_DCI slot=%u.%u PDU walk desync "
                "(consumed=%u body=%u err=%d n_pdus=%u) -> dropping.",
                sfn, slot, r.off, body_len, r.error, n_pdus);
        return -1;
    }
    if (n_pdcch == 0) return 0;

    nfapi_nr_ul_dci_request_t* ul =
        (nfapi_nr_ul_dci_request_t*)calloc(1, sizeof(*ul));
    if (ul == NULL) {
        SM_Logs(LOG_ERROR, _P7_, "[L2->L1 P7] UL_DCI calloc failed.");
        return -1;
    }
    ul->header.phy_id     = (uint16_t)v->phy_id;
    ul->header.message_id = NFAPI_NR_PHY_MSG_TYPE_UL_DCI_REQUEST;
    ul->SFN  = sfn;
    ul->Slot = slot;

    unsigned out = 0;
    for (unsigned i = 0; i < n_pdcch && out < NFAPI_NR_MAX_NB_CORESETS; ++i) {
        nfapi_nr_ul_dci_request_pdus_t* p = &ul->ul_dci_pdu_list[out++];
        p->PDUType = 0;   // PDCCH PDU (only valid value)
        p->PDUSize = (uint16_t)sizeof(nfapi_nr_dl_tti_pdcch_pdu);
        fill_pdcch_rel15(&p->pdcch_pdu.pdcch_pdu_rel15, &pdcch[i]);
    }
    ul->numPdus = (uint8_t)out;

    int rc = oai_vnf_send_p7(ctx, (nfapi_p7_message_header_t*)&ul->header);
    free(ul);
    return (rc == 0) ? 1 : -1;
}

#endif /* OAI_OCUDU */
