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

#include "message_stats.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

// nFAPI struct definitions consumed by the serialize_nfapi_* helpers below.
// These are the open-nFAPI SCF interface headers (same lineage XFAPI used),
// exposed to xfapi_main via its include dirs. Only the OAI_OCUDU mode pulls in
// the open-nFAPI tree and uses these serializers; other modes (e.g.
// AERIAL_OCUDU) build message_stats.c for its core API only.
#ifdef OAI_OCUDU
#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

message_stats_t g_message_stats[MAX_MESSAGE_STATS];
volatile atomic_uint_fast32_t g_stats_index = 0;

void init_message_stats(void) {
    memset(g_message_stats, 0, sizeof(g_message_stats));
    atomic_store(&g_stats_index, 0);
}

uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void add_message_stats(const char* msg_type, int sfn, int slot, int pdu_size,
                       int num_pdus, const char* content, uint64_t ipc_latency_ns) {
    if (!msg_type || !content) {
        return;
    }

    uint32_t current_index = atomic_fetch_add(&g_stats_index, 1) % MAX_MESSAGE_STATS;
    message_stats_t* stats = &g_message_stats[current_index];

    stats->timestamp_ns = get_timestamp_ns();
    strncpy(stats->message_type, msg_type, MAX_MESSAGE_TYPE_LEN - 1);
    stats->message_type[MAX_MESSAGE_TYPE_LEN - 1] = '\0';
    stats->sfn = sfn;
    stats->slot = slot;
    stats->pdu_size = pdu_size;
    stats->num_pdus = num_pdus;
    stats->ipc_latency_ns = ipc_latency_ns;
    strncpy(stats->message_content, content, MAX_MESSAGE_CONTENT_LEN - 1);
    stats->message_content[MAX_MESSAGE_CONTENT_LEN - 1] = '\0';
}

static void escape_json_string(FILE* fp, const char* str) {
    const char* p = str;
    while (*p) {
        switch (*p) {
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '"':  fputs("\\\"", fp); break;
            case '\b': fputs("\\b", fp); break;
            case '\f': fputs("\\f", fp); break;
            default:
                if ((unsigned char)*p < 32) {
                    fprintf(fp, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, fp);
                }
                break;
        }
        p++;
    }
}

void dump_message_stats_to_json(void) {
    FILE* fp = fopen("generated_logs/message_stats.json", "w");
    if (!fp) {
        return;
    }

    uint32_t total_messages = atomic_load(&g_stats_index);
    uint32_t messages_to_dump = (total_messages > MAX_MESSAGE_STATS) ? MAX_MESSAGE_STATS : total_messages;
    uint32_t start_index = (total_messages > MAX_MESSAGE_STATS) ? (total_messages % MAX_MESSAGE_STATS) : 0;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"total_messages_captured\": %u,\n", total_messages);
    fprintf(fp, "  \"messages_in_dump\": %u,\n", messages_to_dump);
    fprintf(fp, "  \"messages\": [\n");

    for (uint32_t i = 0; i < messages_to_dump; i++) {
        uint32_t index = (start_index + i) % MAX_MESSAGE_STATS;
        message_stats_t* stats = &g_message_stats[index];

        if (stats->timestamp_ns == 0) {
            continue;
        }

        fprintf(fp, "    {\n");
        fprintf(fp, "      \"timestamp_ns\": %lu,\n", stats->timestamp_ns);
        fprintf(fp, "      \"message_type\": \"%s\",\n", stats->message_type);
        fprintf(fp, "      \"sfn\": %d,\n", stats->sfn);
        fprintf(fp, "      \"slot\": %d,\n", stats->slot);
        fprintf(fp, "      \"pdu_size\": %d,\n", stats->pdu_size);
        fprintf(fp, "      \"num_pdus\": %d,\n", stats->num_pdus);
        fprintf(fp, "      \"ipc_latency_ns\": %lu,\n", stats->ipc_latency_ns);
        fprintf(fp, "      \"message_content\": \"");
        escape_json_string(fp, stats->message_content);
        fprintf(fp, "\"\n");
        fprintf(fp, "    }");

        if (i < messages_to_dump - 1) {
            fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
}

// nFAPI message serializers: each renders the translated nFAPI struct into a
// human-readable key=value dump used as the dashboard's message_content.
// Compiled only for OAI_OCUDU (the mode that provides the open-nFAPI headers
// and calls these from its P7 translators).
#ifdef OAI_OCUDU

void serialize_nfapi_dl_tti_request_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_dl_tti_request_t* nfapi_dl_tti = (const nfapi_nr_dl_tti_request_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n" 
        "header.message_length=%d\n"
        "SFN=%d\n"
        "Slot=%d\n"
        "dl_tti_request_body.nPDUs=%d\n"
        "dl_tti_request_body.nGroup=%d\n",
        nfapi_dl_tti->header.message_id,
        nfapi_dl_tti->header.phy_id,
        nfapi_dl_tti->header.message_length,
        nfapi_dl_tti->SFN,
        nfapi_dl_tti->Slot,
        nfapi_dl_tti->dl_tti_request_body.nPDUs,
        nfapi_dl_tti->dl_tti_request_body.nGroup);
    
    // Add detailed PDU information (first few PDUs to avoid buffer overflow)
    int remaining = max_len - written;
    if (remaining > 200 && nfapi_dl_tti->dl_tti_request_body.nPDUs > 0) {
        int pdu_limit = (nfapi_dl_tti->dl_tti_request_body.nPDUs > 3) ? 3 : nfapi_dl_tti->dl_tti_request_body.nPDUs;
        for (int i = 0; i < pdu_limit && remaining > 100; i++) {
            int pdu_written = snprintf(output + written, remaining,
                "pdu[%d].PDUType=%d\n"
                "pdu[%d].PDUSize=%d\n",
                i, nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].PDUType,
                i, nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].PDUSize);
            written += pdu_written;
            remaining -= pdu_written;
            
            // Add specific NFAPI PDU details based on type
            if (remaining > 100) {
                switch (nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].PDUType) {
                    case 0: { // NFAPI PDCCH PDU
                        const nfapi_nr_dl_tti_pdcch_pdu_rel15_t* pdcch = &nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].pdcch_pdu.pdcch_pdu_rel15;
                        pdu_written = snprintf(output + written, remaining,
                            "\n----PDCCH----\n"
                            "BWPSize=%d\n"
                            "BWPStart=%d\n"
                            "SubcarrierSpacing=%d\n"
                            "CyclicPrefix=%d\n"
                            "StartSymbolIndex=%d\n"
                            "DurationSymbols=%d\n"
                            "CceRegMappingType=%d\n"
                            "RegBundleSize=%d\n"
                            "InterleaverSize=%d\n"
                            "CoreSetType=%d\n"
                            "ShiftIndex=%d\n"
                            "precoderGranularity=%d\n"
                            "numDlDci=%d\n"
                            "FreqDomainResource= ",
                            pdcch->BWPSize,
                            pdcch->BWPStart,
                            pdcch->SubcarrierSpacing,
                            pdcch->CyclicPrefix,
                            pdcch->StartSymbolIndex,
                            pdcch->DurationSymbols,
                            pdcch->CceRegMappingType,
                            pdcch->RegBundleSize,
                            pdcch->InterleaverSize,
                            pdcch->CoreSetType,
                            pdcch->ShiftIndex,
                            pdcch->precoderGranularity,
                            pdcch->numDlDci);
                        written += pdu_written;
                        remaining -= pdu_written;

                        // Print FreqDomainResource as hex bytes
                        for (int k = 0; k < 6 && remaining > 5; k++) {
                            pdu_written = snprintf(output + written, remaining, " %u", pdcch->FreqDomainResource[k]);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }
                        pdu_written = snprintf(output + written, remaining, "\n");
                        written += pdu_written;
                        remaining -= pdu_written;

                        // Add DCI PDU details
                        int dci_limit = (pdcch->numDlDci > 2) ? 2 : pdcch->numDlDci;
                        for (int j = 0; j < dci_limit && remaining > 100; j++) {
                            const nfapi_nr_dl_dci_pdu_t* dci_pdu = &pdcch->dci_pdu[j];
                            pdu_written = snprintf(output + written, remaining,
                                "dci[%d].RNTI=%d\n"
                                "dci[%d].ScramblingId=%d\n"
                                "dci[%d].ScramblingRNTI=%d\n"
                                "dci[%d].CceIndex=%d\n"
                                "dci[%d].AggregationLevel=%d\n"
                                "dci[%d].beta_PDCCH_1_0=%d\n"
                                "dci[%d].powerControlOffsetSS=%d\n"
                                "dci[%d].PayloadSizeBits=%d\n"
                                "dci[%d].precodingAndBeamforming=0x...\n"
                                "dci[%d].Payload=\n",
                                j, dci_pdu->RNTI,
                                j, dci_pdu->ScramblingId,
                                j, dci_pdu->ScramblingRNTI,
                                j, dci_pdu->CceIndex,
                                j, dci_pdu->AggregationLevel,
                                j, dci_pdu->beta_PDCCH_1_0,
                                j, dci_pdu->powerControlOffsetSS,
                                j, dci_pdu->PayloadSizeBits,
                                j,
                                j);
                            written += pdu_written;
                            remaining -= pdu_written;

                            int payload_bytes = (dci_pdu->PayloadSizeBits + 7) / 8;
                            for (int p = 0; p < payload_bytes && remaining > 6; p++) {
                                pdu_written = snprintf(output + written, remaining, "Payload[%d] = 0x%02X\n", p, dci_pdu->Payload[p]);
                                written += pdu_written;
                                remaining -= pdu_written;
                            }

                        }
                        if (pdcch->numDlDci > 2) {
                            pdu_written = snprintf(output + written, remaining,
                                "...(showing first 2 of %d DCIs)\n", pdcch->numDlDci);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }



                        break;
                    }
                    case 1: { // NFAPI PDSCH PDU
                        const nfapi_nr_dl_tti_pdsch_pdu_rel15_t* pdsch = &nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].pdsch_pdu.pdsch_pdu_rel15;
                        pdu_written = snprintf(output + written, remaining,
                            "\n----PDSCH----\n"
                            "pduBitmap=%d\n"
                            "rnti=%d\n"
                            "pduIndex=%d\n"
                            "BWPSize=%d\n"
                            "BWPStart=%d\n"
                            "SubcarrierSpacing=%d\n"
                            "CyclicPrefix=%d\n"
                            "NrOfCodewords=%d\n"
                            "targetCodeRate[0]=%d, targetCodeRate[1]=%d\n"
                            "qamModOrder[0]=%d, qamModOrder[1]=%d\n"
                            "mcsIndex[0]=%d, mcsIndex[1]=%d\n"
                            "mcsTable[0]=%d, mcsTable[1]=%d\n"
                            "rvIndex[0]=%d, rvIndex[1]=%d\n"
                            "TBSize[0]=%d, TBSize[1]=%d\n"
                            "dataScramblingId=%d\n"
                            "nrOfLayers=%d\n"
                            "transmissionScheme=%d\n"
                            "refPoint=%d\n"
                            "dlDmrsSymbPos=%d\n"
                            "dmrsConfigType=%d\n"
                            "dlDmrsScramblingId=%d\n"
                            "SCID=%d\n"
                            "numDmrsCdmGrpsNoData=%d\n"
                            "dmrsPorts=%d\n"
                            "resourceAlloc=%d\n"
                            "rbStart=%d\n"
                            "rbSize=%d\n"
                            "VRBtoPRBMapping=%d\n"
                            "StartSymbolIndex=%d\n"
                            "NrOfSymbols=%d\n"
                            "PTRSPortIndex=%d\n"
                            "PTRSTimeDensity=%d\n"
                            "PTRSFreqDensity=%d\n"
                            "PTRSReOffset=%d\n"
                            "nEpreRatioOfPDSCHToPTRS=%d\n"
                            "precodingAndBeamforming.num_prgs=%d\n"
                            "precodingAndBeamforming.prg_size=%d\n"
                            "precodingAndBeamforming.dig_bf_interfaces=%d\n"
                            "maintenance_parms_v3.ldpcBaseGraph=%d\n"
                            "maintenance_parms_v3.tbSizeLbrmBytes=%d\n",
                            pdsch->pduBitmap,
                            pdsch->rnti,
                            pdsch->pduIndex,
                            pdsch->BWPSize,
                            pdsch->BWPStart,
                            pdsch->SubcarrierSpacing,
                            pdsch->CyclicPrefix,
                            pdsch->NrOfCodewords,
                            pdsch->targetCodeRate[0], pdsch->targetCodeRate[1],
                            pdsch->qamModOrder[0], pdsch->qamModOrder[1],
                            pdsch->mcsIndex[0], pdsch->mcsIndex[1],
                            pdsch->mcsTable[0], pdsch->mcsTable[1],
                            pdsch->rvIndex[0], pdsch->rvIndex[1],
                            pdsch->TBSize[0], pdsch->TBSize[1],
                            pdsch->dataScramblingId,
                            pdsch->nrOfLayers,
                            pdsch->transmissionScheme,
                            pdsch->refPoint,
                            pdsch->dlDmrsSymbPos,
                            pdsch->dmrsConfigType,
                            pdsch->dlDmrsScramblingId,
                            pdsch->SCID,
                            pdsch->numDmrsCdmGrpsNoData,
                            pdsch->dmrsPorts,
                            pdsch->resourceAlloc,
                            pdsch->rbStart,
                            pdsch->rbSize,
                            pdsch->VRBtoPRBMapping,
                            pdsch->StartSymbolIndex,
                            pdsch->NrOfSymbols,
                            pdsch->PTRSPortIndex,
                            pdsch->PTRSTimeDensity,
                            pdsch->PTRSFreqDensity,
                            pdsch->PTRSReOffset,
                            pdsch->nEpreRatioOfPDSCHToPTRS,
                            pdsch->precodingAndBeamforming.num_prgs,
                            pdsch->precodingAndBeamforming.prg_size,
                            pdsch->precodingAndBeamforming.dig_bf_interfaces,
                            pdsch->maintenance_parms_v3.ldpcBaseGraph,
                            pdsch->maintenance_parms_v3.tbSizeLbrmBytes);
                        written += pdu_written;
                        remaining -= pdu_written;
                        break;
                    }
                    case 2: { // NFAPI CSI-RS PDU 
                        const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t* csi_rs = &nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].csi_rs_pdu.csi_rs_pdu_rel15;
                        pdu_written = snprintf(output + written, remaining,
                            "\n----CSI_RS----\n"
                            "bwp_size=%d\n"
                            "bwp_start=%d\n"
                            "subcarrier_spacing=%d\n"
                            "cyclic_prefix=%d\n"
                            "start_rb=%d\n"
                            "nr_of_rbs=%d\n"
                            "csi_type=%d\n"
                            "row=%d\n"
                            "freq_domain=%d\n"
                            "symb_l0=%d\n"
                            "symb_l1=%d\n"
                            "cdm_type=%d\n"
                            "freq_density=%d\n"
                            "scramb_id=%d\n"
                            "power_control_offset=%d\n"
                            "power_control_offset_ss=%d\n",
                            csi_rs->bwp_size,
                            csi_rs->bwp_start,
                            csi_rs->subcarrier_spacing,
                            csi_rs->cyclic_prefix,
                            csi_rs->start_rb,
                            csi_rs->nr_of_rbs,
                            csi_rs->csi_type,
                            csi_rs->row,
                            csi_rs->freq_domain,
                            csi_rs->symb_l0,
                            csi_rs->symb_l1,
                            csi_rs->cdm_type,
                            csi_rs->freq_density,
                            csi_rs->scramb_id,
                            csi_rs->power_control_offset,
                            csi_rs->power_control_offset_ss);
                        written += pdu_written;
                        remaining -= pdu_written;
                        break;
                    }
                    case 3: { // NFAPI SSB PDU
                        const nfapi_nr_dl_tti_ssb_pdu_rel15_t* ssb = &nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].ssb_pdu.ssb_pdu_rel15;
                        pdu_written = snprintf(output + written, remaining,
                            "\n-----SSB-----\n"
                            "PhysCellId=%d\n"
                            "BetaPss=%d\n"
                            "SsbBlockIndex=%d\n"
                            "SsbSubcarrierOffset=%d\n"
                            "ssbOffsetPointA=%d\n"
                            "bchPayloadFlag=%d\n"
                            "bchPayload=%d\n"
                            "ssbRsrp=%d\n",
                            ssb->PhysCellId,
                            ssb->BetaPss,
                            ssb->SsbBlockIndex,
                            ssb->SsbSubcarrierOffset,
                            ssb->ssbOffsetPointA,
                            ssb->bchPayloadFlag,
                            ssb->bchPayload,
                            ssb->ssbRsrp);
                        written += pdu_written;
                        remaining -= pdu_written;
                        break;
                    }
                    default:
                        pdu_written = snprintf(output + written, remaining,
                            "nfapi_pdu[%d].unknown_type=%d\n", i, nfapi_dl_tti->dl_tti_request_body.dl_tti_pdu_list[i].PDUType);
                        written += pdu_written;
                        remaining -= pdu_written;
                        break;
                }
            }
        }
        if (nfapi_dl_tti->dl_tti_request_body.nPDUs > 3) {
            snprintf(output + written, remaining, "...(showing first 3 of %d NFAPI PDUs)\n", nfapi_dl_tti->dl_tti_request_body.nPDUs);
        }
    }
}

void serialize_nfapi_ul_tti_request_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_ul_tti_request_t* nfapi_ul_tti = (const nfapi_nr_ul_tti_request_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n"
        "header.message_length=%d\n"
        "SFN=%d\n"
        "Slot=%d\n"
        "n_pdus=%d\n"
        "rach_present=%d\n"
        "n_ulsch=%d\n"
        "n_ulcch=%d\n"
        "n_group=%d\n",
        nfapi_ul_tti->header.message_id,
        nfapi_ul_tti->header.phy_id,
        nfapi_ul_tti->header.message_length,
        nfapi_ul_tti->SFN,
        nfapi_ul_tti->Slot,
        nfapi_ul_tti->n_pdus,
        nfapi_ul_tti->rach_present,
        nfapi_ul_tti->n_ulsch,
        nfapi_ul_tti->n_ulcch,
        nfapi_ul_tti->n_group);

    // Add PDU details (first few to avoid buffer overflow)
    int remaining = max_len - written;
    if (remaining > 200 && nfapi_ul_tti->n_pdus > 0) {
        int pdu_limit = (nfapi_ul_tti->n_pdus > 3) ? 3 : nfapi_ul_tti->n_pdus;
        for (int i = 0; i < pdu_limit && remaining > 100; i++) {
            int pdu_written = snprintf(output + written, remaining,
                "\n-- PDU[%d] --\n"
                "pdu_type=%d\n"
                "pdu_size=%d\n",
                i, nfapi_ul_tti->pdus_list[i].pdu_type,
                i, nfapi_ul_tti->pdus_list[i].pdu_size);
            written += pdu_written;
            remaining -= pdu_written;

            // Add specific PDU details based on type
            if (remaining > 100) {
                switch (nfapi_ul_tti->pdus_list[i].pdu_type) {
                    case NFAPI_NR_UL_CONFIG_PRACH_PDU_TYPE: {
                        const nfapi_nr_prach_pdu_t* prach = &nfapi_ul_tti->pdus_list[i].prach_pdu;
                        pdu_written = snprintf(output + written, remaining,
                            "\n----PRACH PDU----\n"
                            "phys_cell_id=%d\n"
                            "num_prach_ocas=%d\n"
                            "prach_format=%d\n"
                            "num_ra=%d\n"
                            "prach_start_symbol=%d\n"
                            "num_cs=%d\n",
                            prach->phys_cell_id,
                            prach->num_prach_ocas,
                            prach->prach_format,
                            prach->num_ra,
                            prach->prach_start_symbol,
                            prach->num_cs);
                        written += pdu_written;
                        remaining -= pdu_written;

                        if (remaining > 100) {
                            const nfapi_nr_ul_beamforming_t* bf = &prach->beamforming;
                            pdu_written = snprintf(output + written, remaining,
                                "beamforming.trp_scheme=%d\n"
                                "beamforming.num_prgs=%d\n"
                                "beamforming.prg_size=%d\n"
                                "beamforming.dig_bf_interface=%d\n",
                                bf->trp_scheme, bf->num_prgs, bf->prg_size, bf->dig_bf_interface);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }
                        break;
                    }
                    case NFAPI_NR_UL_CONFIG_PUSCH_PDU_TYPE: {
                        const nfapi_nr_pusch_pdu_t* pusch = &nfapi_ul_tti->pdus_list[i].pusch_pdu;
                        pdu_written = snprintf(output + written, remaining,
                            "\n----PUSCH PDU----\n"
                            "pdu_bit_map=%d\n"
                            "rnti=%d\n"
                            "handle=%u\n"
                            "bwp_size=%d\n"
                            "bwp_start=%d\n"
                            "subcarrier_spacing=%d\n"
                            "cyclic_prefix=%d\n"
                            "target_code_rate=%d\n"
                            "qam_mod_order=%d\n"
                            "mcs_index=%d\n"
                            "mcs_table=%d\n"
                            "transform_precoding=%d\n"
                            "data_scrambling_id=%d\n"
                            "nrOfLayers=%d\n"
                            "ul_dmrs_symb_pos=0x%x\n"
                            "dmrs_config_type=%d\n"
                            "ul_dmrs_scrambling_id=%d\n"
                            "pusch_identity=%d\n"
                            "scid=%d\n"
                            "num_dmrs_cdm_grps_no_data=%d\n"
                            "dmrs_ports=0x%x\n"
                            "resource_alloc=%d\n"
                            "rb_bitmap[0-3]=0x%02x%02x%02x%02x\n"
                            "rb_start=%d\n"
                            "rb_size=%d\n"
                            "vrb_to_prb_mapping=%d\n"
                            "frequency_hopping=%d\n"
                            "tx_direct_current_location=%d\n"
                            "uplink_frequency_shift_7p5khz=%d\n"
                            "start_symbol_index=%d\n"
                            "nr_of_symbols=%d\n",
                            pusch->pdu_bit_map, pusch->rnti, pusch->handle,
                            pusch->bwp_size, pusch->bwp_start, pusch->subcarrier_spacing, pusch->cyclic_prefix,
                            pusch->target_code_rate, pusch->qam_mod_order, pusch->mcs_index, pusch->mcs_table,
                            pusch->transform_precoding, pusch->data_scrambling_id, pusch->nrOfLayers,
                            pusch->ul_dmrs_symb_pos, pusch->dmrs_config_type, pusch->ul_dmrs_scrambling_id,
                            pusch->pusch_identity, pusch->scid, pusch->num_dmrs_cdm_grps_no_data, pusch->dmrs_ports,
                            pusch->resource_alloc, pusch->rb_bitmap[0], pusch->rb_bitmap[1], pusch->rb_bitmap[2], pusch->rb_bitmap[3],
                            pusch->rb_start, pusch->rb_size, pusch->vrb_to_prb_mapping, pusch->frequency_hopping,
                            pusch->tx_direct_current_location, pusch->uplink_frequency_shift_7p5khz,
                            pusch->start_symbol_index, pusch->nr_of_symbols);
                        written += pdu_written;
                        remaining -= pdu_written;

                        if (pusch->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_DATA && remaining > 100) {
                            const nfapi_nr_pusch_data_t* d = &pusch->pusch_data;
                            pdu_written = snprintf(output + written, remaining,
                                "pusch_data.rv_index=%d\n"
                                "pusch_data.harq_process_id=%d\n"
                                "pusch_data.new_data_indicator=%d\n"
                                "pusch_data.tb_size=%u\n"
                                "pusch_data.num_cb=%d\n"
                                "pusch_data.cb_present_and_position[0]=0x%02x\n",
                                d->rv_index, d->harq_process_id, d->new_data_indicator, d->tb_size, d->num_cb, d->cb_present_and_position[0]);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }

                        if (pusch->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_UCI && remaining > 100) {
                            const nfapi_nr_pusch_uci_t* u = &pusch->pusch_uci;
                            pdu_written = snprintf(output + written, remaining,
                                "pusch_uci.harq_ack_bit_length=%d\n"
                                "pusch_uci.csi_part1_bit_length=%d\n"
                                "pusch_uci.csi_part2_bit_length=%d\n"
                                "pusch_uci.alpha_scaling=%d\n"
                                "pusch_uci.beta_offset_harq_ack=%d\n"
                                "pusch_uci.beta_offset_csi1=%d\n"
                                "pusch_uci.beta_offset_csi2=%d\n",
                                u->harq_ack_bit_length, u->csi_part1_bit_length, u->csi_part2_bit_length,
                                u->alpha_scaling, u->beta_offset_harq_ack, u->beta_offset_csi1, u->beta_offset_csi2);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }

                        if (pusch->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS && remaining > 100) {
                            const nfapi_nr_pusch_ptrs_t* p = &pusch->pusch_ptrs;
                            pdu_written = snprintf(output + written, remaining,
                                "pusch_ptrs.num_ptrs_ports=%d\n"
                                "pusch_ptrs.ptrs_time_density=%d\n"
                                "pusch_ptrs.ptrs_freq_density=%d\n"
                                "pusch_ptrs.ul_ptrs_power=%d\n",
                                p->num_ptrs_ports, p->ptrs_time_density, p->ptrs_freq_density, p->ul_ptrs_power);
                            written += pdu_written;
                            remaining -= pdu_written;
                            int port_limit = (p->num_ptrs_ports > 2) ? 2 : p->num_ptrs_ports;
                            for(int k=0; k < port_limit && remaining > 50; ++k) {
                                pdu_written = snprintf(output + written, remaining,
                                    "  port[%d].index=0x%x, dmrs_port=%d, re_offset=%d\n",
                                    k, p->ptrs_ports_list[k].ptrs_port_index, p->ptrs_ports_list[k].ptrs_dmrs_port, p->ptrs_ports_list[k].ptrs_re_offset);
                                written += pdu_written;
                                remaining -= pdu_written;
                            }
                        }

                        if (pusch->pdu_bit_map & PUSCH_PDU_BITMAP_DFTS_OFDM && remaining > 100) {
                            const nfapi_nr_dfts_ofdm_t* dfts = &pusch->dfts_ofdm;
                            pdu_written = snprintf(output + written, remaining,
                                "dfts_ofdm.low_papr_group_number=%d\n"
                                "dfts_ofdm.low_papr_sequence_number=%d\n"
                                "dfts_ofdm.ul_ptrs_sample_density=%d\n"
                                "dfts_ofdm.ul_ptrs_time_density_transform_precoding=%d\n",
                                dfts->low_papr_group_number, dfts->low_papr_sequence_number, dfts->ul_ptrs_sample_density, dfts->ul_ptrs_time_density_transform_precoding);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }

                        if (remaining > 150) {
                            const nfapi_nr_ul_beamforming_t* bf = &pusch->beamforming;
                            pdu_written = snprintf(output + written, remaining,
                                "beamforming.trp_scheme=%d\n"
                                "beamforming.num_prgs=%d\n"
                                "beamforming.prg_size=%d\n"
                                "beamforming.dig_bf_interface=%d\n",
                                bf->trp_scheme, bf->num_prgs, bf->prg_size, bf->dig_bf_interface);
                            written += pdu_written;
                            remaining -= pdu_written;

                            const nfapi_v3_pusch_maintenance_parameters_t* maint = &pusch->maintenance_parms_v3;
                             pdu_written = snprintf(output + written, remaining,
                                "maintenance.ldpcBaseGraph=%d\n"
                                "maintenance.tbSizeLbrmBytes=%u\n",
                                maint->ldpcBaseGraph, maint->tbSizeLbrmBytes);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }
                        break;
                    }
                    case NFAPI_NR_UL_CONFIG_PUCCH_PDU_TYPE: {
                        const nfapi_nr_pucch_pdu_t* pucch = &nfapi_ul_tti->pdus_list[i].pucch_pdu;
                        pdu_written = snprintf(output + written, remaining,
                            "\n----PUCCH PDU----\n"
                            "rnti=%d\n"
                            "handle=%u\n"
                            "bwp_size=%d\n"
                            "bwp_start=%d\n"
                            "subcarrier_spacing=%d\n"
                            "cyclic_prefix=%d\n"
                            "format_type=%d\n"
                            "multi_slot_tx_indicator=%d\n"
                            "pi_2bpsk=%d\n"
                            "prb_start=%d\n"
                            "prb_size=%d\n"
                            "start_symbol_index=%d\n"
                            "nr_of_symbols=%d\n"
                            "freq_hop_flag=%d\n"
                            "second_hop_prb=%d\n"
                            "group_hop_flag=%d\n"
                            "sequence_hop_flag=%d\n"
                            "hopping_id=%d\n"
                            "initial_cyclic_shift=%d\n"
                            "data_scrambling_id=%d\n"
                            "time_domain_occ_idx=%d\n"
                            "pre_dft_occ_idx=%d\n"
                            "pre_dft_occ_len=%d\n"
                            "add_dmrs_flag=%d\n"
                            "dmrs_scrambling_id=%d\n"
                            "dmrs_cyclic_shift=%d\n"
                            "sr_flag=%d\n"
                            "bit_len_harq=%d\n"
                            "bit_len_csi_part1=%d\n"
                            "bit_len_csi_part2=%d\n",
                            pucch->rnti, pucch->handle, pucch->bwp_size, pucch->bwp_start, pucch->subcarrier_spacing, pucch->cyclic_prefix,
                            pucch->format_type, pucch->multi_slot_tx_indicator, pucch->pi_2bpsk, pucch->prb_start, pucch->prb_size,
                            pucch->start_symbol_index, pucch->nr_of_symbols, pucch->freq_hop_flag, pucch->second_hop_prb,
                            pucch->group_hop_flag, pucch->sequence_hop_flag, pucch->hopping_id, pucch->initial_cyclic_shift,
                            pucch->data_scrambling_id, pucch->time_domain_occ_idx, pucch->pre_dft_occ_idx, pucch->pre_dft_occ_len,
                            pucch->add_dmrs_flag, pucch->dmrs_scrambling_id, pucch->dmrs_cyclic_shift, pucch->sr_flag,
                            pucch->bit_len_harq, pucch->bit_len_csi_part1, pucch->bit_len_csi_part2);
                        written += pdu_written;
                        remaining -= pdu_written;

                        if (remaining > 100) {
                            const nfapi_nr_ul_beamforming_t* bf = &pucch->beamforming;
                            pdu_written = snprintf(output + written, remaining,
                                "beamforming.trp_scheme=%d\n"
                                "beamforming.num_prgs=%d\n"
                                "beamforming.prg_size=%d\n"
                                "beamforming.dig_bf_interface=%d\n",
                                bf->trp_scheme, bf->num_prgs, bf->prg_size, bf->dig_bf_interface);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }
                        break;
                    }
                    case NFAPI_NR_UL_CONFIG_SRS_PDU_TYPE: {
                        const nfapi_nr_srs_pdu_t* srs = &nfapi_ul_tti->pdus_list[i].srs_pdu;
                        pdu_written = snprintf(output + written, remaining,
                            "\n----SRS PDU----\n"
                            "rnti=%d\n"
                            "handle=%u\n"
                            "bwp_size=%d\n"
                            "bwp_start=%d\n"
                            "subcarrier_spacing=%d\n"
                            "cyclic_prefix=%d\n"
                            "num_ant_ports=%d\n"
                            "num_symbols=%d\n"
                            "num_repetitions=%d\n"
                            "time_start_position=%d\n"
                            "config_index=%d\n"
                            "sequence_id=%d\n"
                            "bandwidth_index=%d\n"
                            "comb_size=%d\n"
                            "comb_offset=%d\n"
                            "cyclic_shift=%d\n"
                            "frequency_position=%d\n"
                            "frequency_shift=%d\n"
                            "frequency_hopping=%d\n"
                            "group_or_sequence_hopping=%d\n"
                            "resource_type=%d\n"
                            "t_srs=%d\n"
                            "t_offset=%d\n",
                            srs->rnti, srs->handle, srs->bwp_size, srs->bwp_start, srs->subcarrier_spacing, srs->cyclic_prefix,
                            srs->num_ant_ports, srs->num_symbols, srs->num_repetitions, srs->time_start_position, srs->config_index,
                            srs->sequence_id, srs->bandwidth_index, srs->comb_size, srs->comb_offset, srs->cyclic_shift,
                            srs->frequency_position, srs->frequency_shift, srs->frequency_hopping, srs->group_or_sequence_hopping,
                            srs->resource_type, srs->t_srs, srs->t_offset);
                        written += pdu_written;
                        remaining -= pdu_written;

                         if (remaining > 100) {
                            const nfapi_nr_ul_beamforming_t* bf = &srs->beamforming;
                            pdu_written = snprintf(output + written, remaining,
                                "beamforming.trp_scheme=%d\n"
                                "beamforming.num_prgs=%d\n"
                                "beamforming.prg_size=%d\n"
                                "beamforming.dig_bf_interface=%d\n",
                                bf->trp_scheme, bf->num_prgs, bf->prg_size, bf->dig_bf_interface);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }

                        if (remaining > 300) {
                            const nfapi_v4_srs_parameters_t* v4 = &srs->srs_parameters_v4;
                            pdu_written = snprintf(output + written, remaining,
                                "srs_v4.srs_bandwidth_size=%d\n"
                                "srs_v4.usage=0x%x\n"
                                "srs_v4.report_type[0-3]=%d,%d,%d,%d\n"
                                "srs_v4.singular_Value_representation=%d\n"
                                "srs_v4.iq_representation=%d\n"
                                "srs_v4.prg_size=%d\n"
                                "srs_v4.num_total_ue_antennas=%d\n"
                                "srs_v4.ue_antennas_in_this_srs_resource_set=0x%x\n"
                                "srs_v4.sampled_ue_antennas=0x%x\n"
                                "srs_v4.report_scope=%d\n"
                                "srs_v4.num_ul_spatial_streams_ports=%d\n",
                                v4->srs_bandwidth_size, v4->usage, v4->report_type[0], v4->report_type[1], v4->report_type[2], v4->report_type[3],
                                v4->singular_Value_representation, v4->iq_representation, v4->prg_size, v4->num_total_ue_antennas,
                                v4->ue_antennas_in_this_srs_resource_set, v4->sampled_ue_antennas, v4->report_scope, v4->num_ul_spatial_streams_ports);
                            written += pdu_written;
                            remaining -= pdu_written;
                        }
                        break;
                    }
                    default:
                        pdu_written = snprintf(output + written, remaining, "pdu[%d].type_name=UNKNOWN\n", i);
                        written += pdu_written;
                        remaining -= pdu_written;
                        break;
                }
            }
        }
        if (nfapi_ul_tti->n_pdus > 3 && remaining > 50) {
            snprintf(output + written, remaining, "...(showing first 3 of %d UL PDUs)\n", nfapi_ul_tti->n_pdus);
        }
    }

    remaining = max_len - written;
    if (remaining > 100 && nfapi_ul_tti->n_group > 0) {
        written += snprintf(output + written, remaining, "\n-- UE Groups --\n");
        remaining = max_len - written;
        int group_limit = (nfapi_ul_tti->n_group > 2) ? 2 : nfapi_ul_tti->n_group;
        for (int i = 0; i < group_limit && remaining > 50; i++) {
            int group_written = snprintf(output + written, remaining,
                "group[%d].n_ue=%d\n",
                i, nfapi_ul_tti->groups_list[i].n_ue);
            written += group_written;
            remaining -= group_written;

            int ue_limit = (nfapi_ul_tti->groups_list[i].n_ue > 2) ? 2 : nfapi_ul_tti->groups_list[i].n_ue;
            for (int j = 0; j < ue_limit && remaining > 50; j++) {
                int ue_written = snprintf(output + written, remaining,
                    "  group[%d].ue_list[%d].pdu_idx=%d\n",
                    i, j, nfapi_ul_tti->groups_list[i].ue_list[j].pdu_idx);
                written += ue_written;
                remaining -= ue_written;
            }
            if (nfapi_ul_tti->groups_list[i].n_ue > 2 && remaining > 50) {
                snprintf(output + written, remaining, "  ...(showing first 2 of %d UEs in this group)\n", nfapi_ul_tti->groups_list[i].n_ue);
            }
        }
    }
}

void serialize_nfapi_ul_dci_request_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_ul_dci_request_t* nfapi_ul_dci = (const nfapi_nr_ul_dci_request_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n"
        "header.message_length=%d\n"
        "SFN=%d\n"
        "Slot=%d\n"
        "numPdus=%d\n",
        nfapi_ul_dci->header.message_id,
        nfapi_ul_dci->header.phy_id,
        nfapi_ul_dci->header.message_length,
        nfapi_ul_dci->SFN,
        nfapi_ul_dci->Slot,
        nfapi_ul_dci->numPdus);

    int remaining = max_len - written;
    if (remaining > 200 && nfapi_ul_dci->numPdus > 0) {
        int pdu_limit = (nfapi_ul_dci->numPdus > 3) ? 3 : nfapi_ul_dci->numPdus;
        for (int i = 0; i < pdu_limit && remaining > 100; i++) {
            const nfapi_nr_ul_dci_request_pdus_t* ul_dci_pdu = &nfapi_ul_dci->ul_dci_pdu_list[i];
            
            int pdu_written = snprintf(output + written, remaining,
                "pdu[%d].PDUType=%d\n"
                "pdu[%d].PDUSize=%d\n",
                i, ul_dci_pdu->PDUType,
                i, ul_dci_pdu->PDUSize);
            written += pdu_written;
            remaining -= pdu_written;

            if (ul_dci_pdu->PDUType == 0) { // PDCCH PDU
                const nfapi_nr_dl_tti_pdcch_pdu_rel15_t* pdcch = &ul_dci_pdu->pdcch_pdu.pdcch_pdu_rel15;
                pdu_written = snprintf(output + written, remaining,
                    "\n----PDCCH----\n"
                    "BWPSize=%d\n"
                    "BWPStart=%d\n"
                    "SubcarrierSpacing=%d\n"
                    "CyclicPrefix=%d\n"
                    "StartSymbolIndex=%d\n"
                    "DurationSymbols=%d\n"
                    "CceRegMappingType=%d\n"
                    "RegBundleSize=%d\n"
                    "InterleaverSize=%d\n"
                    "CoreSetType=%d\n"
                    "ShiftIndex=%d\n"
                    "precoderGranularity=%d\n"
                    "numDlDci=%d\n"
                    "FreqDomainResource= ",
                    pdcch->BWPSize,
                    pdcch->BWPStart,
                    pdcch->SubcarrierSpacing,
                    pdcch->CyclicPrefix,
                    pdcch->StartSymbolIndex,
                    pdcch->DurationSymbols,
                    pdcch->CceRegMappingType,
                    pdcch->RegBundleSize,
                    pdcch->InterleaverSize,
                    pdcch->CoreSetType,
                    pdcch->ShiftIndex,
                    pdcch->precoderGranularity,
                    pdcch->numDlDci);
                written += pdu_written;
                remaining -= pdu_written;

                // Print FreqDomainResource as hex bytes
                for (int k = 0; k < 6 && remaining > 5; k++) {
                    pdu_written = snprintf(output + written, remaining, " %u", pdcch->FreqDomainResource[k]);
                    written += pdu_written;
                    remaining -= pdu_written;
                }
                pdu_written = snprintf(output + written, remaining, "\n");
                written += pdu_written;
                remaining -= pdu_written;

                // Add DCI PDU details
                int dci_limit = (pdcch->numDlDci > 2) ? 2 : pdcch->numDlDci;
                for (int j = 0; j < dci_limit && remaining > 100; j++) {
                    const nfapi_nr_dl_dci_pdu_t* dci_pdu = &pdcch->dci_pdu[j];
                    pdu_written = snprintf(output + written, remaining,
                        "dci[%d].RNTI=%d\n"
                        "dci[%d].ScramblingId=%d\n"
                        "dci[%d].ScramblingRNTI=%d\n"
                        "dci[%d].CceIndex=%d\n"
                        "dci[%d].AggregationLevel=%d\n"
                        "dci[%d].beta_PDCCH_1_0=%d\n"
                        "dci[%d].powerControlOffsetSS=%d\n"
                        "dci[%d].PayloadSizeBits=%d\n"
                        "dci[%d].precodingAndBeamforming=0x...\n"
                        "dci[%d].Payload=\n",
                        j, dci_pdu->RNTI,
                        j, dci_pdu->ScramblingId,
                        j, dci_pdu->ScramblingRNTI,
                        j, dci_pdu->CceIndex,
                        j, dci_pdu->AggregationLevel,
                        j, dci_pdu->beta_PDCCH_1_0,
                        j, dci_pdu->powerControlOffsetSS,
                        j, dci_pdu->PayloadSizeBits,
                        j,
                        j);
                    written += pdu_written;
                    remaining -= pdu_written;

                    int payload_bytes = (dci_pdu->PayloadSizeBits + 7) / 8;
                    for (int p = 0; p < payload_bytes && remaining > 6; p++) {
                        pdu_written = snprintf(output + written, remaining, "Payload[%d] = 0x%02X\n", p, dci_pdu->Payload[p]);
                        written += pdu_written;
                        remaining -= pdu_written;
                    }

                }
                if (pdcch->numDlDci > 2) {
                    pdu_written = snprintf(output + written, remaining,
                        "...(showing first 2 of %d DCIs)\n", pdcch->numDlDci);
                    written += pdu_written;
                    remaining -= pdu_written;
                }
            } else {
                pdu_written = snprintf(output + written, remaining,
                    "pdu[%d].type_name=UNKNOWN (PDUType=%d)\n", i, ul_dci_pdu->PDUType);
                written += pdu_written;
                remaining -= pdu_written;
            }
        }
        if (nfapi_ul_dci->numPdus > 3) {
            snprintf(output + written, remaining, "...(showing first 3 of %d UL DCI PDUs)\n", nfapi_ul_dci->numPdus);
        }
    }
}

void serialize_nfapi_tx_data_request_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_tx_data_request_t* nfapi_tx_data = (const nfapi_nr_tx_data_request_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n"
        "header.message_length=%d\n"
        "SFN=%d\n"
        "Slot=%d\n"
        "Number_of_PDUs=%d\n",
        nfapi_tx_data->header.message_id,
        nfapi_tx_data->header.phy_id,
        nfapi_tx_data->header.message_length,
        nfapi_tx_data->SFN,
        nfapi_tx_data->Slot,
        nfapi_tx_data->Number_of_PDUs);
    
    // Add PDU details
    int remaining = max_len - written;
    if (remaining > 200 && nfapi_tx_data->Number_of_PDUs > 0) {
        int pdu_limit = (nfapi_tx_data->Number_of_PDUs > 3) ? 3 : nfapi_tx_data->Number_of_PDUs;
        for (int i = 0; i < pdu_limit && remaining > 100; i++) {
            const nfapi_nr_pdu_t* tx_pdu = &nfapi_tx_data->pdu_list[i];

            int pdu_written = snprintf(output + written, remaining,
                "\n----TX PDU[%d]----\n"
                "PDU_length=%d\n"
                "PDU_index=%d\n"
                "num_TLV=%d\n",
                i, tx_pdu->PDU_length,
                tx_pdu->PDU_index,
                tx_pdu->num_TLV);
            written += pdu_written;
            remaining -= pdu_written;
            
            // Add TLV details
            if (remaining > 200 && tx_pdu->num_TLV > 0) {
                int tlv_limit = (tx_pdu->num_TLV > 3) ? 3 : tx_pdu->num_TLV;
                for (int j = 0; j < tlv_limit && remaining > 50; j++) {
                    const nfapi_nr_tx_data_request_tlv_t* tlv = &tx_pdu->TLVs[j];
                    int tlv_written = snprintf(output + written, remaining,
                        "  TLV[%d].tag=%d\n"
                        "  TLV[%d].length=%d\n",
                        j, tlv->tag,
                        j, tlv->length);
                    written += tlv_written;
                    remaining -= tlv_written;
                    
                    // Add TLV data (whole payload as hex string)
                    if (remaining > 3 && tlv->length > 0) {
                        int hex_written = snprintf(output + written, remaining, "  TLV[%d].data_hex=", j);
                        written += hex_written;
                        remaining -= hex_written;
                        
                        uint8_t* data_bytes;
                        if (tlv->tag == 0) {
                            data_bytes = (uint8_t*)tlv->value.direct;
                        } else {
                            data_bytes = (uint8_t*)tlv->value.ptr;
                        }
                        
                        for (int k = 0; k < tlv->length && remaining > 3; k++) {
                            int byte_written = snprintf(output + written, remaining, "%02X ", data_bytes[k]);
                            written += byte_written;
                            remaining -= byte_written;
                        }
                        
                        if (remaining > 1) {
                            output[written++] = '\n';
                            remaining--;
                        }
                    }
                }
                
                if (tx_pdu->num_TLV > 3 && remaining > 30) {
                    int tlv_suffix_written = snprintf(output + written, remaining, "  ...(showing first 3 of %d TLVs)\n", tx_pdu->num_TLV);
                    written += tlv_suffix_written;
                    remaining -= tlv_suffix_written;
                }
            }
        }
        if (nfapi_tx_data->Number_of_PDUs > 3) {
            snprintf(output + written, remaining, "...(showing first 3 of %d TX Data PDUs)\n", nfapi_tx_data->Number_of_PDUs);
        }
    }
}


void serialize_nfapi_rx_data_indication_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_rx_data_indication_t* nfapi_rx_data = (const nfapi_nr_rx_data_indication_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n"
        "header.message_length=%d\n"
        "sfn=%d\n"
        "slot=%d\n"
        "number_of_pdus=%d\n",
        nfapi_rx_data->header.message_id,
        nfapi_rx_data->header.phy_id,
        nfapi_rx_data->header.message_length,
        nfapi_rx_data->sfn,
        nfapi_rx_data->slot,
        nfapi_rx_data->number_of_pdus);
    
    // Add PDU details
    int remaining = max_len - written;
    if (remaining > 200 && nfapi_rx_data->number_of_pdus > 0) {
        int pdu_limit = (nfapi_rx_data->number_of_pdus > 3) ? 3 : nfapi_rx_data->number_of_pdus;
        for (int i = 0; i < pdu_limit && remaining > 100; i++) {
            const nfapi_nr_rx_data_pdu_t* rx_pdu = &nfapi_rx_data->pdu_list[i];

            int pdu_written = snprintf(output + written, remaining,
                "\n----RX PDU[%d]----\n"
                "handle=%d\n"
                "rnti=%d\n"
                "harq_id=%d\n"
                "pdu_length=%d\n"
                "ul_cqi=%d\n"
                "timing_advance=%d\n"
                "rssi=%d\n",
                i, rx_pdu->handle,
                rx_pdu->rnti,
                rx_pdu->harq_id,
                rx_pdu->pdu_length,
                rx_pdu->ul_cqi,
                rx_pdu->timing_advance,
                rx_pdu->rssi);
            written += pdu_written;
            remaining -= pdu_written;
            
            // Add PDU data content (whole payload as hex string)
            if (remaining > 3 && rx_pdu->pdu && rx_pdu->pdu_length > 0) {
                int hex_written = snprintf(output + written, remaining, "pdu[%d].data_hex=", i);
                written += hex_written;
                remaining -= hex_written;
                
                for (int j = 0; j < rx_pdu->pdu_length && remaining > 3; j++) {
                    int byte_written = snprintf(output + written, remaining, "%02X ", rx_pdu->pdu[j]);
                    written += byte_written;
                    remaining -= byte_written;
                }
                
                if (remaining > 1) {
                    output[written++] = '\n';
                    remaining--;
                }
            }
        }
        if (nfapi_rx_data->number_of_pdus > 3) {
            snprintf(output + written, remaining, "...(showing first 3 of %d RX Data PDUs)\n", nfapi_rx_data->number_of_pdus);
        }
    }
}


void serialize_nfapi_crc_indication_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_crc_indication_t* nfapi_crc = (const nfapi_nr_crc_indication_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n"
        "header.message_length=%d\n"
        "sfn=%d\n"
        "slot=%d\n"
        "number_crcs=%d\n",
        nfapi_crc->header.message_id,
        nfapi_crc->header.phy_id,
        nfapi_crc->header.message_length,
        nfapi_crc->sfn,
        nfapi_crc->slot,
        nfapi_crc->number_crcs);
    
    // Add CRC details
    int remaining = max_len - written;
    if (remaining > 100 && nfapi_crc->number_crcs > 0) {
        int crc_limit = (nfapi_crc->number_crcs > 5) ? 5 : nfapi_crc->number_crcs;
        for (int i = 0; i < crc_limit && remaining > 50; i++) {
            int crc_written = snprintf(output + written, remaining,
                "crc[%d].rnti=%d\n"
                "crc[%d].harq_id=%d\n"
                "crc[%d].tb_crc_status=%d\n"
                "crc[%d].num_cb=%d\n"
                "crc[%d].ul_cqi=%d\n"
                "crc[%d].timing_advance=%d\n"
                "crc[%d].rssi=%d\n",
                i, nfapi_crc->crc_list[i].rnti,
                i, nfapi_crc->crc_list[i].harq_id,
                i, nfapi_crc->crc_list[i].tb_crc_status,
                i, nfapi_crc->crc_list[i].num_cb,
                i, nfapi_crc->crc_list[i].ul_cqi,
                i, nfapi_crc->crc_list[i].timing_advance,
                i, nfapi_crc->crc_list[i].rssi);
            written += crc_written;
            remaining -= crc_written;
        }
        if (nfapi_crc->number_crcs > 5) {
            snprintf(output + written, remaining, "...(showing first 5 of %d CRC entries)\n", nfapi_crc->number_crcs);
        }
    }
}

void serialize_nfapi_uci_indication_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_uci_indication_t* nfapi_uci = (const nfapi_nr_uci_indication_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n"
        "header.message_length=%d\n"
        "sfn=%d\n"
        "slot=%d\n"
        "num_ucis=%d\n",
        nfapi_uci->header.message_id,
        nfapi_uci->header.phy_id,
        nfapi_uci->header.message_length,
        nfapi_uci->sfn,
        nfapi_uci->slot,
        nfapi_uci->num_ucis);
    
    int remaining = max_len - written;
    if (remaining > 200 && nfapi_uci->num_ucis > 0) {
        int uci_limit = (nfapi_uci->num_ucis > 3) ? 3 : nfapi_uci->num_ucis;
        for (int i = 0; i < uci_limit && remaining > 100; i++) {
            const nfapi_nr_uci_t* uci_pdu = &nfapi_uci->uci_list[i];
            
            int uci_written = snprintf(output + written, remaining,
                "\n----UCI PDU[%d]----\n"
                "pdu_type=%d\n"
                "pdu_size=%d\n",
                i, uci_pdu->pdu_type,
                uci_pdu->pdu_size);
            written += uci_written;
            remaining -= uci_written;

            if (uci_pdu->pdu_type == 0) { // PUSCH PDU
                const nfapi_nr_uci_pusch_pdu_t* pusch = &uci_pdu->pusch_pdu;
                uci_written = snprintf(output + written, remaining,
                    "rnti=%d\n"
                    "handle=%d\n"
                    "ul_cqi=%d\n"
                    "timing_advance=%d\n"
                    "rssi=%d\n"
                    "harq_crc=%d\n"
                    "harq_bit_len=%d\n",
                    pusch->rnti,
                    pusch->handle,
                    pusch->ul_cqi,
                    pusch->timing_advance,
                    pusch->rssi,
                    pusch->harq.harq_crc,
                    pusch->harq.harq_bit_len);
                written += uci_written;
                remaining -= uci_written;
            } else if (uci_pdu->pdu_type == 1) { // PUCCH Format 0 or 1
                const nfapi_nr_uci_pucch_pdu_format_0_1_t* pucch_0_1 = &uci_pdu->pucch_pdu_format_0_1;
                uci_written = snprintf(output + written, remaining,
                    "rnti=%d\n"
                    "handle=%d\n"
                    "pucch_format=%d\n"
                    "ul_cqi=%d\n"
                    "timing_advance=%d\n"
                    "rssi=%d\n"
                    "sr_indication=%d\n"
                    "sr_confidence_level=%d\n"
                    "harq_num_harq=%d\n"
                    "harq_confidence_level=%d\n",
                    pucch_0_1->rnti,
                    pucch_0_1->handle,
                    pucch_0_1->pucch_format,
                    pucch_0_1->ul_cqi,
                    pucch_0_1->timing_advance,
                    pucch_0_1->rssi,
                    pucch_0_1->sr.sr_indication,
                    pucch_0_1->sr.sr_confidence_level,
                    pucch_0_1->harq.num_harq,
                    pucch_0_1->harq.harq_confidence_level);
                written += uci_written;
                remaining -= uci_written;
                for(int j = 0; j < pucch_0_1->harq.num_harq && remaining > 50; j++) {
                    uci_written = snprintf(output + written, remaining,
                        "harq[%d].harq_value=%d\n",
                        j, pucch_0_1->harq.harq_list[j].harq_value);
                    written += uci_written;
                    remaining -= uci_written;
                }
            } else if (uci_pdu->pdu_type == 2) { // PUCCH Format 2, 3, or 4
                const nfapi_nr_uci_pucch_pdu_format_2_3_4_t* pucch_2_3_4 = &uci_pdu->pucch_pdu_format_2_3_4;
                uci_written = snprintf(output + written, remaining,
                    "rnti=%d\n"
                    "handle=%d\n"
                    "pucch_format=%d\n"
                    "ul_cqi=%d\n"
                    "timing_advance=%d\n"
                    "rssi=%d\n"
                    "harq_crc=%d\n"
                    "harq_bit_len=%d\n"
                    "csi_part1_crc=%d\n"
                    "csi_part1_bit_len=%d\n"
                    "csi_part2_crc=%d\n"
                    "csi_part2_bit_len=%d\n",
                    pucch_2_3_4->rnti,
                    pucch_2_3_4->handle,
                    pucch_2_3_4->pucch_format,
                    pucch_2_3_4->ul_cqi,
                    pucch_2_3_4->timing_advance,
                    pucch_2_3_4->rssi,
                    pucch_2_3_4->harq.harq_crc,
                    pucch_2_3_4->harq.harq_bit_len,
                    pucch_2_3_4->csi_part1.csi_part1_crc,
                    pucch_2_3_4->csi_part1.csi_part1_bit_len,
                    pucch_2_3_4->csi_part2.csi_part2_crc,
                    pucch_2_3_4->csi_part2.csi_part2_bit_len);
                written += uci_written;
                remaining -= uci_written;
            } else {
                uci_written = snprintf(output + written, remaining,
                    "pdu_type_name=UNKNOWN (pdu_type=%d)\n", uci_pdu->pdu_type);
                written += uci_written;
                remaining -= uci_written;
            }
        }
        if (nfapi_uci->num_ucis > 3) {
            snprintf(output + written, remaining, "...(showing first 3 of %d UCI entries)\n", nfapi_uci->num_ucis);
        }
    }
}
void serialize_nfapi_rach_indication_message(char* output, int max_len, const void* msg_ptr) {
    const nfapi_nr_rach_indication_t* nfapi_rach = (const nfapi_nr_rach_indication_t*)msg_ptr;
    int written = snprintf(output, max_len,
        "header.message_id=%d\n"
        "header.phy_id=%d\n"
        "header.message_length=%d\n"
        "sfn=%d\n"
        "slot=%d\n"
        "number_of_pdus=%d\n",
        nfapi_rach->header.message_id,
        nfapi_rach->header.phy_id,
        nfapi_rach->header.message_length,
        nfapi_rach->sfn,
        nfapi_rach->slot,
        nfapi_rach->number_of_pdus);
    
    // Add RACH PDU details
    int remaining = max_len - written;
    if (remaining > 200 && nfapi_rach->number_of_pdus > 0) {
        int pdu_limit = (nfapi_rach->number_of_pdus > 3) ? 3 : nfapi_rach->number_of_pdus;
        for (int i = 0; i < pdu_limit && remaining > 100; i++) {
            const nfapi_nr_prach_indication_pdu_t* rach_pdu = &nfapi_rach->pdu_list[i];

            int pdu_written = snprintf(output + written, remaining,
                "\n----RACH PDU[%d]----\n"
                "RACH_PDU[%d].phy_cell_id=%d\n"
                "RACH_PDU[%d].symbol_index=%d\n"
                "RACH_PDU[%d].slot_index=%d\n"
                "RACH_PDU[%d].freq_index=%d\n"
                "RACH_PDU[%d].avg_rssi=%d\n"
                "RACH_PDU[%d].avg_snr=%d\n"
                "RACH_PDU[%d].num_preamble=%d\n",
                i, rach_pdu->phy_cell_id,
                i, rach_pdu->symbol_index,
                i, rach_pdu->slot_index,
                i, rach_pdu->freq_index,
                i, rach_pdu->avg_rssi,
                i, rach_pdu->avg_snr,
                i, rach_pdu->num_preamble);
            written += pdu_written;
            remaining -= pdu_written;

            // Add preamble details
            int preamble_limit = (rach_pdu->num_preamble > 2) ? 2 : rach_pdu->num_preamble;
            for (int j = 0; j < preamble_limit && remaining > 50; j++) {
                const nfapi_nr_prach_indication_preamble_t* preamble = &rach_pdu->preamble_list[j];
                int preamble_written = snprintf(output + written, remaining,
                    "  preamble[%d].preamble_index=%d\n"
                    "  preamble[%d].timing_advance=%d\n"
                    "  preamble[%d].preamble_pwr=%d\n",
                    j, preamble->preamble_index,
                    j, preamble->timing_advance,
                    j, preamble->preamble_pwr);
                written += preamble_written;
                remaining -= preamble_written;
            }
            if (rach_pdu->num_preamble > 2) {
                snprintf(output + written, remaining, "  ...(showing first 2 of %d preambles)\n", rach_pdu->num_preamble);
                written += strlen(output + written);
                remaining -= strlen(output + written);
            }
        }
        if (nfapi_rach->number_of_pdus > 3) {
            snprintf(output + written, remaining, "...(showing first 3 of %d RACH PDUs)\n", nfapi_rach->number_of_pdus);
        }
    }
}


#endif /* OAI_OCUDU */
