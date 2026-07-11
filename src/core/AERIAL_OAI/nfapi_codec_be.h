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

// Prototypes for the big-endian nFAPI codec entry points (nfapi_codec_be).
// Its public API is renamed to be_* at build time (see nfapi_codec_be_rename.h),
// so the OAI-facing glue includes this header and calls the be_* names, leaving
// the plain nfapi_nr_* names bound to nothing here and the little-endian Aerial
// codec keeping fapi_nr_*. The struct types are shared and come from the codec's
// public headers. Signatures mirror nfapi_nr_interface.h exactly.

#ifndef AERIAL_OAI_NFAPI_CODEC_BE_H
#define AERIAL_OAI_NFAPI_CODEC_BE_H

#ifdef AERIAL_OAI

#include <stdbool.h>
#include <stdint.h>

#include "nfapi_interface.h"
#include "nfapi_nr_interface_scf.h"

int  be_nfapi_nr_p5_message_pack(void* pMessageBuf, uint32_t messageBufLen,
                                 void* pPackedBuf, uint32_t packedBufLen,
                                 nfapi_p4_p5_codec_config_t* config);
bool be_nfapi_nr_p5_message_unpack(void* pMessageBuf, uint32_t messageBufLen,
                                   void* pUnpackedBuf, uint32_t unpackedBufLen,
                                   nfapi_p4_p5_codec_config_t* config);
bool be_nfapi_nr_p5_message_header_unpack(void* pMessageBuf, uint32_t messageBufLen,
                                          void* pUnpackedBuf, uint32_t unpackedBufLen,
                                          nfapi_p4_p5_codec_config_t* config);

int  be_nfapi_nr_p7_message_pack(void* pMessageBuf, void* pPackedBuf,
                                 uint32_t packedBufLen,
                                 nfapi_p7_codec_config_t* config);
bool be_nfapi_nr_p7_message_header_unpack(void* pMessageBuf, uint32_t messageBufLen,
                                          void* pUnpackedBuf, uint32_t unpackedBufLen,
                                          nfapi_p7_codec_config_t* config);
bool be_nfapi_nr_p7_message_unpack(void* pMessageBuf, uint32_t messageBufLen,
                                   void* pUnpackedBuf, uint32_t unpackedBufLen,
                                   nfapi_p7_codec_config_t* config);
int  be_nfapi_nr_p7_update_checksum(uint8_t* buffer, uint32_t len);

#endif /* AERIAL_OAI */
#endif /* AERIAL_OAI_NFAPI_CODEC_BE_H */
