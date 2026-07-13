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

// Startup banner shared by main.c and app_manager.c so every mode prints the
// same shape: a framed banner naming the mode + topology, then phase-tagged
// [INIT]/[BIND]/[UP] lines. XFAPI_MODE_NAME / XFAPI_MODE_TOPO resolve per mode.

#ifndef XFAPI_STARTUP_BANNER_H
#define XFAPI_STARTUP_BANNER_H

#include "unified_logger.h"

#if defined(OCUDU_OCUDU)
  #define XFAPI_MODE_NAME "OCUDU_OCUDU"
  #define XFAPI_MODE_TOPO "OCUDU(xSM) <-> OCUDU(xSM)"
  #define XFAPI_WAIT_ON   "OCUDU L1 + OCUDU L2 peers"
#elif defined(OAI_OCUDU)
  #define XFAPI_MODE_NAME "OAI_OCUDU"
  #define XFAPI_MODE_TOPO "OAI(nFAPI) <-> OCUDU(xSM)"
  #define XFAPI_WAIT_ON   "OAI PNF connect + OCUDU MASTER"
#elif defined(AERIAL_OCUDU)
  #define XFAPI_MODE_NAME "AERIAL_OCUDU"
  #define XFAPI_MODE_TOPO "Aerial(nvIPC) <-> OCUDU(xSM)"
  #define XFAPI_WAIT_ON   "Aerial PRIMARY + OCUDU MASTER"
#elif defined(AERIAL_OAI)
  #define XFAPI_MODE_NAME "AERIAL_OAI"
  #define XFAPI_MODE_TOPO "Aerial(nvIPC) <-> OAI(nFAPI)"
  #define XFAPI_WAIT_ON   "Aerial PRIMARY + OAI VNF connect"
#else
  #define XFAPI_MODE_NAME "UNKNOWN"
  #define XFAPI_MODE_TOPO "no mode defined"
  #define XFAPI_WAIT_ON   "peers"
#endif

// Print the framed startup banner. Uses SM_Logs only.
#define XFAPI_LOG_BANNER()                                                     \
    do {                                                                       \
        SM_Logs(LOG_INFO, _XFAPI_,                                             \
                "========================================================");   \
        SM_Logs(LOG_INFO, _XFAPI_,                                             \
                "  xFAPI  |  " XFAPI_MODE_NAME "  |  " XFAPI_MODE_TOPO);        \
        SM_Logs(LOG_INFO, _XFAPI_,                                             \
                "========================================================");   \
    } while (0)

#endif /* XFAPI_STARTUP_BANNER_H */
