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

// AERIAL_OAI links the open-nFAPI codec TWICE in one process: little-endian for
// the Aerial/SCF side (nfapi_codec) and big-endian for the OAI/nFAPI side
// (nfapi_codec_be). The endianness is a compile-time switch, so both builds
// share every function name and would collide at link time.
//
// This header is force-included (-include) into the big-endian codec build to
// rename the six nFAPI entry points the OAI glue calls to be_*. The OAI glue
// then calls the be_* names (declared in nfapi_codec_be.h), leaving the plain
// names bound to the little-endian Aerial codec. A version script keeps every
// other codec symbol (push16/pull16, the fapi_nr_* helpers) local to each
// library, so the big-endian entry points really pack big-endian.

#ifndef AERIAL_OAI_NFAPI_CODEC_BE_RENAME_H
#define AERIAL_OAI_NFAPI_CODEC_BE_RENAME_H

#define nfapi_nr_p5_message_pack          be_nfapi_nr_p5_message_pack
#define nfapi_nr_p5_message_unpack        be_nfapi_nr_p5_message_unpack
#define nfapi_nr_p5_message_header_unpack be_nfapi_nr_p5_message_header_unpack
#define nfapi_nr_p7_message_pack          be_nfapi_nr_p7_message_pack
#define nfapi_nr_p7_message_unpack        be_nfapi_nr_p7_message_unpack
#define nfapi_nr_p7_message_header_unpack be_nfapi_nr_p7_message_header_unpack
#define nfapi_nr_p7_update_checksum       be_nfapi_nr_p7_update_checksum

#endif
