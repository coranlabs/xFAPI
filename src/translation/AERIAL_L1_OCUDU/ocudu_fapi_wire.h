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

// OCUDU FAPI wire format (hand-rolled, pure C) for the AERIAL_OCUDU bridge:
// reads/writes the exact little-endian byte layout OCUDU's C++ FAPI serializer
// produces over xSM (40-byte xsm header + buffer_reader/writer body
// primitives). Bounds-checked; callers inspect ->error after parsing.

#ifndef AERIAL_L1_OCUDU_OCUDU_FAPI_WIRE_H
#define AERIAL_L1_OCUDU_OCUDU_FAPI_WIRE_H

#ifdef AERIAL_OCUDU

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// OCUDU FAPI message type IDs (ocudu_india fapi_message_type_id.h). These are
// also the value OCUDU puts in xsm_msg_t.type_id.
// ---------------------------------------------------------------------------
#define OCUDU_FAPI_PARAM_REQUEST    0x00
#define OCUDU_FAPI_PARAM_RESPONSE   0x01
#define OCUDU_FAPI_CONFIG_REQUEST   0x02
#define OCUDU_FAPI_CONFIG_RESPONSE  0x03
#define OCUDU_FAPI_START_REQUEST    0x04
#define OCUDU_FAPI_STOP_REQUEST     0x05
#define OCUDU_FAPI_STOP_INDICATION  0x06
#define OCUDU_FAPI_ERROR_INDICATION 0x07
// P7 (data plane).
#define OCUDU_FAPI_DL_TTI_REQUEST     0x80
#define OCUDU_FAPI_UL_TTI_REQUEST     0x81
#define OCUDU_FAPI_SLOT_INDICATION    0x82
#define OCUDU_FAPI_UL_DCI_REQUEST     0x83
#define OCUDU_FAPI_TX_DATA_REQUEST    0x84
#define OCUDU_FAPI_RX_DATA_INDICATION 0x85
#define OCUDU_FAPI_CRC_INDICATION     0x86
#define OCUDU_FAPI_UCI_INDICATION     0x87
#define OCUDU_FAPI_SRS_INDICATION     0x88
#define OCUDU_FAPI_RACH_INDICATION    0x89

// ---------------------------------------------------------------------------
// fapi_xsm_msg_header (ocudu_india fapi_xsm_message_header.h). 40 bytes:
//   off 0  : p_next             (ptr, transport-local — zeroed by us)
//   off 8  : p_tx_data_elm_list (ptr, transport-local — zeroed by us)
//   off 16 : msg_type           (u8)
//   off 17 : num_messages_in_block (u8)
//   off 20 : msg_len            (u32 LE, BODY-only length, excludes header)
//   off 24 : align_offset       (u32)
//   off 32 : time_stamp         (u64 LE, ns)
// ---------------------------------------------------------------------------
#define OCUDU_XSM_HEADER_SIZE        40u
#define OCUDU_XSM_OFF_MSG_TYPE       16u
#define OCUDU_XSM_OFF_NUM_MSG        17u
#define OCUDU_XSM_OFF_MSG_LEN        20u
#define OCUDU_XSM_OFF_TIMESTAMP      32u

// Parse the xSM header of a buffer received from OCUDU-L2. On success returns 0
// and sets *msg_type, *body (pointer into buf past the header) and *body_len.
// Returns -1 if the buffer is too small or the declared body overruns it.
static inline int ocudu_xsm_hdr_parse(const uint8_t* buf, uint32_t size,
                                      uint8_t* msg_type,
                                      const uint8_t** body, uint32_t* body_len)
{
    if (buf == NULL || size < OCUDU_XSM_HEADER_SIZE) {
        return -1;
    }
    uint32_t mlen = 0;
    memcpy(&mlen, buf + OCUDU_XSM_OFF_MSG_LEN, 4);
    if ((uint64_t)OCUDU_XSM_HEADER_SIZE + mlen > size) {
        return -1;
    }
    if (msg_type) *msg_type = buf[OCUDU_XSM_OFF_MSG_TYPE];
    if (body)     *body     = buf + OCUDU_XSM_HEADER_SIZE;
    if (body_len) *body_len = mlen;
    return 0;
}

// Build an xSM header at dst (must have OCUDU_XSM_HEADER_SIZE bytes) for a
// message we send toward OCUDU-L2. Pointers/align are zeroed; one message per
// block. Caller writes the body immediately after.
static inline void ocudu_xsm_hdr_build(uint8_t* dst, uint8_t msg_type,
                                       uint32_t body_len, uint64_t time_stamp)
{
    memset(dst, 0, OCUDU_XSM_HEADER_SIZE);
    dst[OCUDU_XSM_OFF_MSG_TYPE] = msg_type;
    dst[OCUDU_XSM_OFF_NUM_MSG]  = 1;
    memcpy(dst + OCUDU_XSM_OFF_MSG_LEN, &body_len, 4);
    memcpy(dst + OCUDU_XSM_OFF_TIMESTAMP, &time_stamp, 8);
}

// ---------------------------------------------------------------------------
// Little-endian buffer reader (mirrors fapi_serial::buffer_reader). Every
// read bounds-checks; on overrun ->error is latched and reads return 0.
// ---------------------------------------------------------------------------
typedef struct {
    const uint8_t* data;
    uint32_t       len;
    uint32_t       off;
    int            error;   // 0 = ok, 1 = overrun seen
} ocudu_rd_t;

static inline void ocudu_rd_init(ocudu_rd_t* r, const uint8_t* data, uint32_t len)
{
    r->data = data; r->len = len; r->off = 0; r->error = 0;
}

static inline int ocudu_rd_have(ocudu_rd_t* r, uint32_t n)
{
    if (r->error || (uint64_t)r->off + n > r->len) { r->error = 1; return 0; }
    return 1;
}

static inline uint8_t ocudu_rd_u8(ocudu_rd_t* r)
{
    if (!ocudu_rd_have(r, 1)) return 0;
    return r->data[r->off++];
}

static inline int8_t  ocudu_rd_i8(ocudu_rd_t* r)  { return (int8_t)ocudu_rd_u8(r); }
static inline int     ocudu_rd_bool(ocudu_rd_t* r){ return ocudu_rd_u8(r) != 0; }

static inline uint16_t ocudu_rd_u16(ocudu_rd_t* r)
{
    if (!ocudu_rd_have(r, 2)) return 0;
    uint16_t v; memcpy(&v, r->data + r->off, 2); r->off += 2; return v;
}

static inline uint32_t ocudu_rd_u32(ocudu_rd_t* r)
{
    if (!ocudu_rd_have(r, 4)) return 0;
    uint32_t v; memcpy(&v, r->data + r->off, 4); r->off += 4; return v;
}

static inline int32_t ocudu_rd_i32(ocudu_rd_t* r)
{
    if (!ocudu_rd_have(r, 4)) return 0;
    int32_t v; memcpy(&v, r->data + r->off, 4); r->off += 4; return v;
}

static inline uint64_t ocudu_rd_u64(ocudu_rd_t* r)
{
    if (!ocudu_rd_have(r, 8)) return 0;
    uint64_t v; memcpy(&v, r->data + r->off, 8); r->off += 8; return v;
}

static inline int64_t ocudu_rd_i64(ocudu_rd_t* r) { return (int64_t)ocudu_rd_u64(r); }

static inline void ocudu_rd_bytes(ocudu_rd_t* r, void* dst, uint32_t n)
{
    if (!ocudu_rd_have(r, n)) { if (dst) memset(dst, 0, n); return; }
    if (dst) memcpy(dst, r->data + r->off, n);
    r->off += n;
}

// Skip n bytes (e.g. a field we don't need but must step over).
static inline void ocudu_rd_skip(ocudu_rd_t* r, uint32_t n)
{
    if (!ocudu_rd_have(r, n)) return;
    r->off += n;
}

// Skip an OCUDU bounded_bitset: u32 nbits + ceil(nbits/64) u64 chunks
// (fapi_serial::serialize(bounded_bitset)). Length read from the wire, so the
// actual bit count doesn't need to be known in advance.
static inline void ocudu_rd_skip_bitset(ocudu_rd_t* r)
{
    uint32_t nbits  = ocudu_rd_u32(r);
    uint32_t chunks = (nbits + 63u) / 64u;
    ocudu_rd_skip(r, chunks * 8u);
}

// Skip a static_vector<uint8_t>: u16 count + count bytes.
static inline void ocudu_rd_skip_sv_u8(ocudu_rd_t* r)
{
    uint16_t n = ocudu_rd_u16(r);
    ocudu_rd_skip(r, n);
}

// Skip a static_vector<uint16_t>: u16 count + count*2 bytes.
static inline void ocudu_rd_skip_sv_u16(ocudu_rd_t* r)
{
    uint16_t n = ocudu_rd_u16(r);
    ocudu_rd_skip(r, (uint32_t)n * 2u);
}

// ---------------------------------------------------------------------------
// Little-endian buffer writer (mirrors fapi_serial::buffer_writer). Bounds-
// checked; on overflow ->error is latched and writes are dropped.
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t* data;
    uint32_t cap;
    uint32_t off;
    int      error;   // 0 = ok, 1 = overflow seen
} ocudu_wr_t;

static inline void ocudu_wr_init(ocudu_wr_t* w, uint8_t* data, uint32_t cap)
{
    w->data = data; w->cap = cap; w->off = 0; w->error = 0;
}

static inline int ocudu_wr_room(ocudu_wr_t* w, uint32_t n)
{
    if (w->error || (uint64_t)w->off + n > w->cap) { w->error = 1; return 0; }
    return 1;
}

static inline void ocudu_wr_u8(ocudu_wr_t* w, uint8_t v)
{
    if (!ocudu_wr_room(w, 1)) return;
    w->data[w->off++] = v;
}

static inline void ocudu_wr_u16(ocudu_wr_t* w, uint16_t v)
{
    if (!ocudu_wr_room(w, 2)) return;
    memcpy(w->data + w->off, &v, 2); w->off += 2;
}

static inline void ocudu_wr_u32(ocudu_wr_t* w, uint32_t v)
{
    if (!ocudu_wr_room(w, 4)) return;
    memcpy(w->data + w->off, &v, 4); w->off += 4;
}

static inline void ocudu_wr_i32(ocudu_wr_t* w, int32_t v)  { ocudu_wr_u32(w, (uint32_t)v); }

static inline void ocudu_wr_u64(ocudu_wr_t* w, uint64_t v)
{
    if (!ocudu_wr_room(w, 8)) return;
    memcpy(w->data + w->off, &v, 8); w->off += 8;
}

static inline void ocudu_wr_i64(ocudu_wr_t* w, int64_t v)  { ocudu_wr_u64(w, (uint64_t)v); }

static inline void ocudu_wr_bool(ocudu_wr_t* w, int v)     { ocudu_wr_u8(w, v ? 1 : 0); }

static inline void ocudu_wr_bytes(ocudu_wr_t* w, const void* src, uint32_t n)
{
    if (!ocudu_wr_room(w, n)) return;
    memcpy(w->data + w->off, src, n);
    w->off += n;
}

#endif /* AERIAL_OCUDU */
#endif /* AERIAL_L1_OCUDU_OCUDU_FAPI_WIRE_H */
