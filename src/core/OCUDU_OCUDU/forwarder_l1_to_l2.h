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

#ifndef OCUDU_FORWARDER_L1_TO_L2_H
#define OCUDU_FORWARDER_L1_TO_L2_H

struct AppContext;

int  ocudu_fwd_l1_to_l2_start(struct AppContext* ctx);
void ocudu_fwd_l1_to_l2_stop(struct AppContext* ctx);

#endif
