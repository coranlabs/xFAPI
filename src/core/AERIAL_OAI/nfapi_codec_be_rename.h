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

// The open-nFAPI codec is linked twice (little-endian for Aerial, big-endian for
// OAI); both share every symbol name. Force-included (-include) into the
// big-endian build to rename its public entry points to be_*, avoiding a link
// collision with the little-endian codec.

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
