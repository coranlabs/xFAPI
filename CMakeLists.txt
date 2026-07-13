cmake_minimum_required(VERSION 3.15)
project(xFAPI C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_C_FLAGS_DEBUG "-g")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(OAI_OSC "Use OAI as L1 and OSC as L2" OFF)
option(FLEXRAN_OSC "Use FlexRAN as L1 and OSC as L2" OFF)
option(FLEXRAN_OAI "Use FlexRAN as L1 and OAI as L2" OFF)
option(AERIAL_OSC "Use Aerial as L1 and OSC as L2" OFF)
option(OCUDU_OCUDU "Use OCUDU as L1 and OCUDU as L2 (xSM-bridged)" OFF)
option(OAI_OCUDU "Use OAI as L1 (nFAPI/UDP) and OCUDU as L2 (xSM-bridged)" OFF)
option(AERIAL_OCUDU "Use Aerial as L1 (SCF FAPI/nvIPC) and OCUDU as L2 (xSM-bridged)" OFF)
option(AERIAL_OAI "Use Aerial as L1 (SCF FAPI/nvIPC) and OAI as L2 (nFAPI SCTP+UDP)" OFF)
option(L1_SIM_OAI_OSC "Simulated L1: OAI, L2: OSC" OFF)
option(L1_SIM_OAI_OAI "Simulated L1: OAI, L2: OAI" OFF)
option(L1_SIM_FRAN_OSC "Simulated L1: FlexRAN, L2: OSC" OFF)
option(L1_SIM_FRAN_OAI "Simulated L1: FlexRAN, L2: OAI" OFF)
option(L2_SIM_OAI_OAI "Simulated L2: OAI, L1: OAI" OFF)
option(L2_SIM_OAI_FRAN "Simulated L2: OAI, L1: FlexRAN" OFF)
option(L2_SIM_OSC_OAI "Simulated L2: OSC, L1: OAI" OFF)
option(L2_SIM_OSC_FRAN "Simulated L2: OSC, L1: FlexRAN" OFF)

foreach(mode OAI_OSC FLEXRAN_OSC FLEXRAN_OAI AERIAL_OSC OCUDU_OCUDU OAI_OCUDU AERIAL_OCUDU AERIAL_OAI
              L1_SIM_OAI_OSC L1_SIM_OAI_OAI L1_SIM_FRAN_OSC L1_SIM_FRAN_OAI
              L2_SIM_OAI_OAI L2_SIM_OAI_FRAN L2_SIM_OSC_OAI L2_SIM_OSC_FRAN)
    if(${mode})
        add_compile_definitions(${mode})
        message(STATUS "Enabled compile definition: ${mode}")
        list(APPEND ENABLED_MODES ${mode})
    endif()
endforeach()

list(LENGTH ENABLED_MODES ENABLED_MODE_COUNT)
if(ENABLED_MODE_COUNT GREATER 1)
    message(FATAL_ERROR
        "More than one build mode is enabled: ${ENABLED_MODES}. "
        "The modes are mutually exclusive. This usually means a stale build "
        "cache: delete the build directory and reconfigure with a single "
        "-D<MODE>=ON.")
endif()

set(SOURCES
    src/main/main.c
    src/main/app_manager.c
    src/utils/config/yaml_config.c
    src/utils/config/yaml_fail_check.c
    src/utils/logs/unified_logger.c
    src/utils/memory/mem_ops.c
    src/utils/task_mngr/thread_manager.c
    src/utils/task_mngr/itc_queue.c
    src/sim/common/sim_common.c
    src/framework/framework_init.c

)

if(OAI_OSC)
endif()

if(FLEXRAN_OSC)
endif()

if(FLEXRAN_OAI)
endif()

if(L1_SIM_OAI_OSC)
endif()

if(L2_SIM_OSC_OAI)
endif()

if(L1_SIM_FRAN_OSC)
endif()

if(L2_SIM_OSC_FRAN)
endif()

if(L1_SIM_FRAN_OAI)
endif()

if(L2_SIM_OAI_FRAN)
endif()

if(L1_SIM_OAI_OAI)
endif()

if(L2_SIM_OAI_OAI)
endif()

if(AERIAL_OSC)
endif()

if(OCUDU_OCUDU)
    message(STATUS "Adding sources for OCUDU_OCUDU mode")

    list(APPEND SOURCES
        src/framework/dpdk/dpdk_init.c
        src/framework/ocudu_l1_interface.c
        src/framework/ocudu_l2_interface.c
        src/core/OCUDU_OCUDU/forwarder_l1_to_l2.c
        src/core/OCUDU_OCUDU/forwarder_l2_to_l1.c
        src/core/OCUDU_OCUDU/forwarder_split_l1.c
        src/core/OCUDU_OCUDU/forwarder_split_l2.c
        src/core/OCUDU_OCUDU/pair0_master_pool.c

        src/utils/config/yaml_fail_check.c
        src/utils/stats_generation/message_stats.c
    )
endif()

if(OAI_OCUDU)
    message(STATUS "Adding sources for OAI_OCUDU mode")

    list(APPEND SOURCES
        src/framework/dpdk/dpdk_init.c
        src/framework/oai_l1_interface.c
        src/framework/ocudu_l2_interface.c
        src/core/OAI_OCUDU/oai_vnf.c
        src/core/OAI_OCUDU/oai_p7.c
        src/core/OAI_OCUDU/forwarder_l2_to_oai.c

        src/translation/OAI_L1_OCUDU/L2_to_L1/oai_l2_to_l1_p5.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/P5/param_request.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/P5/config_request.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/P5/start_request.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/P5/stop_request.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/oai_l1_to_l2_p5.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/P5/param_response.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/P5/config_response.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/oai_l1_to_l2_p7.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/P7/slot_indication.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/P7/rach_indication.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/P7/crc_indication.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/P7/rx_data_indication.c
        src/translation/OAI_L1_OCUDU/L1_to_L2/P7/uci_indication.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/oai_l2_to_l1_p7.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/P7/dl_tti_request.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/P7/tx_data_request.c
        src/translation/OAI_L1_OCUDU/L2_to_L1/P7/ul_tti_request.c

        src/utils/config/yaml_fail_check.c
        src/utils/stats_generation/message_stats.c
    )

    set(NFAPI_DIR     "${PROJECT_SOURCE_DIR}/src/ipc/nfapi")
    set(NFAPI_UP      "${NFAPI_DIR}/open-nFAPI")
    set(NFAPI_OAICOM  "${NFAPI_DIR}/oai_common")

    set(NFAPI_CODEC_SOURCES
        ${NFAPI_UP}/nfapi/src/nfapi_p4.c
        ${NFAPI_UP}/nfapi/src/nfapi_p5.c
        ${NFAPI_UP}/nfapi/src/nfapi_p7.c
        ${NFAPI_UP}/common/src/nfapi.c
        ${NFAPI_UP}/common/src/debug.c
        ${NFAPI_UP}/fapi/src/nr_fapi.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5_utils.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7_utils.c

        ${NFAPI_OAICOM}/minimal_stub.c
    )

    add_library(nfapi_codec SHARED ${NFAPI_CODEC_SOURCES})
    target_include_directories(nfapi_codec PUBLIC
        ${NFAPI_DIR}
        ${NFAPI_UP}/nfapi/public_inc
        ${NFAPI_UP}/common/public_inc
        ${NFAPI_UP}/fapi/inc
        ${NFAPI_OAICOM}
        ${NFAPI_OAICOM}/common/utils
    )

    target_include_directories(nfapi_codec PRIVATE ${PROJECT_SOURCE_DIR}/src/ipc)

    target_compile_definitions(nfapi_codec PUBLIC MAX_NUM_CCs=1 ENABLE_SOCKET ENABLE_10_02)

    target_compile_options(nfapi_codec PRIVATE -w)
    target_link_libraries(nfapi_codec PUBLIC z pthread m)
endif()

if(AERIAL_OCUDU)
    message(STATUS "Adding sources for AERIAL_OCUDU mode")

    list(APPEND SOURCES
        src/framework/dpdk/dpdk_init.c
        src/framework/aerial_l1_interface.c
        src/framework/ocudu_l2_interface.c
        src/core/AERIAL_OCUDU/aerial_nvipc.c
        src/core/AERIAL_OCUDU/aerial_send.c
        src/core/AERIAL_OCUDU/forwarder_l2_to_aerial.c

        src/translation/AERIAL_L1_OCUDU/L2_to_L1/aerial_l2_to_l1_p5.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/P5/param_request.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/P5/config_request.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/P5/start_request.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/P5/stop_request.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/aerial_l2_to_l1_p7.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/P7/dl_tti_request.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/P7/tx_data_request.c
        src/translation/AERIAL_L1_OCUDU/L2_to_L1/P7/ul_tti_request.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/aerial_l1_to_l2_p5.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P5/param_response.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P5/config_response.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/aerial_l1_to_l2_p7.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P7/slot_indication.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P7/rach_indication.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P7/crc_indication.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P7/rx_data_indication.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P7/uci_indication.c
        src/translation/AERIAL_L1_OCUDU/L1_to_L2/P7/srs_indication.c

        src/utils/config/yaml_fail_check.c
        src/utils/stats_generation/message_stats.c
    )

    set(NFAPI_DIR     "${PROJECT_SOURCE_DIR}/src/ipc/nfapi")
    set(NFAPI_UP      "${NFAPI_DIR}/open-nFAPI")
    set(NFAPI_OAICOM  "${NFAPI_DIR}/oai_common")

    set(NFAPI_AERIAL_SOURCES
        ${NFAPI_UP}/common/src/nfapi.c
        ${NFAPI_UP}/common/src/debug.c
        ${NFAPI_UP}/fapi/src/nr_fapi.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5_utils.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7_utils.c
        ${NFAPI_OAICOM}/minimal_stub.c
    )

    add_library(nfapi_codec SHARED ${NFAPI_AERIAL_SOURCES})
    target_include_directories(nfapi_codec PUBLIC
        ${NFAPI_DIR}
        ${NFAPI_UP}/nfapi/public_inc
        ${NFAPI_UP}/common/public_inc
        ${NFAPI_UP}/fapi/inc
        ${NFAPI_OAICOM}
        ${NFAPI_OAICOM}/common/utils
    )
    target_include_directories(nfapi_codec PRIVATE ${PROJECT_SOURCE_DIR}/src/ipc)

    target_compile_definitions(nfapi_codec PUBLIC
        MAX_NUM_CCs=1 ENABLE_AERIAL FAPI_BYTE_ORDERING_BIG_ENDIAN)
    set_source_files_properties(
        ${NFAPI_UP}/fapi/src/nr_fapi_p5.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5_utils.c
        PROPERTIES COMPILE_DEFINITIONS "ENABLE_10_04")
    target_compile_options(nfapi_codec PRIVATE -w)
    target_link_libraries(nfapi_codec PUBLIC z pthread m)
endif()

if(AERIAL_OAI)
    message(STATUS "Adding sources for AERIAL_OAI mode")

    list(APPEND SOURCES
        src/framework/aerial_oai_l1_interface.c
        src/framework/oai_l2_interface.c
        src/core/AERIAL_OAI/aerial_nvipc.c
        src/core/AERIAL_OAI/aerial_send.c
        src/core/AERIAL_OAI/oai_pnf.c
        src/core/AERIAL_OAI/oai_p7.c
        src/core/AERIAL_OAI/aerial_oai_bridge.c

        src/utils/config/yaml_fail_check.c
        src/utils/stats_generation/message_stats.c
    )

    set(NFAPI_DIR     "${PROJECT_SOURCE_DIR}/src/ipc/nfapi")
    set(NFAPI_UP      "${NFAPI_DIR}/open-nFAPI")
    set(NFAPI_OAICOM  "${NFAPI_DIR}/oai_common")

    set(NFAPI_LE_SOURCES
        ${NFAPI_UP}/common/src/nfapi.c
        ${NFAPI_UP}/common/src/debug.c
        ${NFAPI_UP}/fapi/src/nr_fapi.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5_utils.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7_utils.c
        ${NFAPI_OAICOM}/minimal_stub.c
    )
    set(NFAPI_BE_SOURCES
        ${NFAPI_UP}/nfapi/src/nfapi_p4.c
        ${NFAPI_UP}/nfapi/src/nfapi_p5.c
        ${NFAPI_UP}/nfapi/src/nfapi_p7.c
        ${NFAPI_UP}/common/src/nfapi.c
        ${NFAPI_UP}/common/src/debug.c
        ${NFAPI_UP}/fapi/src/nr_fapi.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p5_utils.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7.c
        ${NFAPI_UP}/fapi/src/nr_fapi_p7_utils.c
        ${NFAPI_OAICOM}/minimal_stub.c
    )

    set(NFAPI_CODEC_INCLUDES
        ${NFAPI_DIR}
        ${NFAPI_UP}/nfapi/public_inc
        ${NFAPI_UP}/common/public_inc
        ${NFAPI_UP}/fapi/inc
        ${NFAPI_OAICOM}
        ${NFAPI_OAICOM}/common/utils
    )

    add_library(nfapi_codec SHARED ${NFAPI_LE_SOURCES})
    target_include_directories(nfapi_codec PUBLIC ${NFAPI_CODEC_INCLUDES})
    target_include_directories(nfapi_codec PRIVATE ${PROJECT_SOURCE_DIR}/src/ipc)
    target_compile_definitions(nfapi_codec PUBLIC
        MAX_NUM_CCs=1 ENABLE_AERIAL ENABLE_10_04 FAPI_BYTE_ORDERING_BIG_ENDIAN)
    target_compile_options(nfapi_codec PRIVATE -w)
    target_link_options(nfapi_codec PRIVATE
        "-Wl,--version-script=${PROJECT_SOURCE_DIR}/src/core/AERIAL_OAI/nfapi_codec_le.map")
    target_link_libraries(nfapi_codec PUBLIC z pthread m)

    add_library(nfapi_codec_be SHARED ${NFAPI_BE_SOURCES})
    target_include_directories(nfapi_codec_be PUBLIC ${NFAPI_CODEC_INCLUDES})
    target_include_directories(nfapi_codec_be PRIVATE ${PROJECT_SOURCE_DIR}/src/ipc)
    target_compile_definitions(nfapi_codec_be PUBLIC
        MAX_NUM_CCs=1 ENABLE_SOCKET ENABLE_10_02)
    target_compile_options(nfapi_codec_be PRIVATE -w
        "-include" "${PROJECT_SOURCE_DIR}/src/core/AERIAL_OAI/nfapi_codec_be_rename.h")
    target_link_options(nfapi_codec_be PRIVATE
        "-Wl,--version-script=${PROJECT_SOURCE_DIR}/src/core/AERIAL_OAI/nfapi_codec_be.map")
    target_link_libraries(nfapi_codec_be PUBLIC z pthread m)
endif()

add_executable(xfapi_main ${SOURCES})

target_include_directories(xfapi_main PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/api_defs
    ${PROJECT_SOURCE_DIR}/include/utils
    ${PROJECT_SOURCE_DIR}/src/utils/logs
    ${PROJECT_SOURCE_DIR}/src/utils/memory
    ${PROJECT_SOURCE_DIR}/src/utils/task_mngr
)

if (AERIAL_OSC)
endif()

if(OAI_OCUDU)
    set(NFAPI_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nfapi")
    target_include_directories(xfapi_main PRIVATE
        ${NFAPI_DIR}
        ${NFAPI_DIR}/open-nFAPI/nfapi/public_inc
        ${NFAPI_DIR}/open-nFAPI/common/public_inc
        ${NFAPI_DIR}/open-nFAPI/fapi/inc
        ${NFAPI_DIR}/open-nFAPI/vnf/public_inc
        ${NFAPI_DIR}/oai_common
        ${NFAPI_DIR}/oai_common/common/utils

        ${PROJECT_SOURCE_DIR}/src/main
        ${PROJECT_SOURCE_DIR}/src/core/OAI_OCUDU
        ${PROJECT_SOURCE_DIR}/src/translation/OAI_L1_OCUDU
        ${PROJECT_SOURCE_DIR}/src/translation/OAI_L1_OCUDU/L2_to_L1
        ${PROJECT_SOURCE_DIR}/src/translation/OAI_L1_OCUDU/L1_to_L2

        ${PROJECT_SOURCE_DIR}/src/utils/stats_generation
    )

    target_compile_definitions(xfapi_main PRIVATE MAX_NUM_CCs=1 ENABLE_SOCKET ENABLE_10_02)

    target_link_libraries(xfapi_main PRIVATE nfapi_codec sctp)
endif()

if(AERIAL_OCUDU)
    if(NOT NVIPC_DIR)
        if(EXISTS "${PROJECT_SOURCE_DIR}/src/ipc/nvipc/nvIPC/libnvipc.so")
            set(NVIPC_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nvipc/nvIPC")
        else()
            set(NVIPC_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nvipc")
        endif()
    endif()
    set(NVIPC_LIB_PATH "${NVIPC_DIR}/libnvipc.so")
    if(NOT EXISTS ${NVIPC_LIB_PATH})
        message(FATAL_ERROR "libnvipc.so not found at ${NVIPC_LIB_PATH}. "
                            "Build the nvIPC sources or pass -DNVIPC_DIR=<path>.")
    endif()

    if(EXISTS "${NVIPC_DIR}/include/nv_ipc.h")
        set(NVIPC_INCLUDE_DIR "${NVIPC_DIR}/include")
    else()
        set(NVIPC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nvipc/include")
    endif()

    set(NFAPI_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nfapi")
    target_include_directories(xfapi_main PRIVATE
        ${NVIPC_INCLUDE_DIR}
        ${PROJECT_SOURCE_DIR}/src/ipc
        ${NFAPI_DIR}
        ${NFAPI_DIR}/open-nFAPI/nfapi/public_inc
        ${NFAPI_DIR}/open-nFAPI/common/public_inc
        ${NFAPI_DIR}/open-nFAPI/fapi/inc
        ${NFAPI_DIR}/oai_common
        ${NFAPI_DIR}/oai_common/common/utils
        ${PROJECT_SOURCE_DIR}/src/main
        ${PROJECT_SOURCE_DIR}/src/core/AERIAL_OCUDU
        ${PROJECT_SOURCE_DIR}/src/translation/AERIAL_L1_OCUDU
        ${PROJECT_SOURCE_DIR}/src/translation/AERIAL_L1_OCUDU/L2_to_L1
        ${PROJECT_SOURCE_DIR}/src/translation/AERIAL_L1_OCUDU/L1_to_L2

        ${PROJECT_SOURCE_DIR}/src/utils/stats_generation
    )
    target_compile_definitions(xfapi_main PRIVATE MAX_NUM_CCs=1 ENABLE_AERIAL)
    target_link_libraries(xfapi_main PRIVATE nfapi_codec ${NVIPC_LIB_PATH} pthread rt)

    get_filename_component(NVIPC_PARENT "${NVIPC_DIR}" DIRECTORY)
    foreach(_dep nvlog fmtlog-shared)
        find_library(NVIPC_${_dep}_LIB NAMES ${_dep}
                     HINTS ${NVIPC_DIR} ${NVIPC_PARENT} NO_DEFAULT_PATH)
        if(NVIPC_${_dep}_LIB)
            target_link_libraries(xfapi_main PRIVATE ${NVIPC_${_dep}_LIB})
        endif()
    endforeach()
    set_target_properties(xfapi_main PROPERTIES
        BUILD_RPATH "${NVIPC_DIR};${NVIPC_PARENT}"
        INSTALL_RPATH "${NVIPC_DIR};${NVIPC_PARENT}"
    )
endif()

if(AERIAL_OAI)
    if(NOT NVIPC_DIR)
        if(EXISTS "${PROJECT_SOURCE_DIR}/src/ipc/nvipc/nvIPC/libnvipc.so")
            set(NVIPC_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nvipc/nvIPC")
        else()
            set(NVIPC_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nvipc")
        endif()
    endif()
    set(NVIPC_LIB_PATH "${NVIPC_DIR}/libnvipc.so")
    if(NOT EXISTS ${NVIPC_LIB_PATH})
        message(FATAL_ERROR "libnvipc.so not found at ${NVIPC_LIB_PATH}. "
                            "Build the nvIPC sources or pass -DNVIPC_DIR=<path>.")
    endif()
    if(EXISTS "${NVIPC_DIR}/include/nv_ipc.h")
        set(NVIPC_INCLUDE_DIR "${NVIPC_DIR}/include")
    else()
        set(NVIPC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nvipc/include")
    endif()

    set(NFAPI_DIR "${PROJECT_SOURCE_DIR}/src/ipc/nfapi")
    target_include_directories(xfapi_main PRIVATE
        ${NVIPC_INCLUDE_DIR}
        ${PROJECT_SOURCE_DIR}/src/ipc
        ${NFAPI_DIR}
        ${NFAPI_DIR}/open-nFAPI/nfapi/public_inc
        ${NFAPI_DIR}/open-nFAPI/common/public_inc
        ${NFAPI_DIR}/open-nFAPI/fapi/inc
        ${NFAPI_DIR}/oai_common
        ${NFAPI_DIR}/oai_common/common/utils
        ${PROJECT_SOURCE_DIR}/src/main
        ${PROJECT_SOURCE_DIR}/src/core/AERIAL_OAI
        ${PROJECT_SOURCE_DIR}/src/utils/stats_generation
    )

    target_compile_definitions(xfapi_main PRIVATE MAX_NUM_CCs=1 ENABLE_AERIAL ENABLE_10_04)
    target_link_libraries(xfapi_main PRIVATE
        nfapi_codec nfapi_codec_be ${NVIPC_LIB_PATH} sctp pthread rt)

    get_filename_component(NVIPC_PARENT "${NVIPC_DIR}" DIRECTORY)
    foreach(_dep nvlog fmtlog-shared)
        find_library(NVIPC_${_dep}_LIB NAMES ${_dep}
                     HINTS ${NVIPC_DIR} ${NVIPC_PARENT} NO_DEFAULT_PATH)
        if(NVIPC_${_dep}_LIB)
            target_link_libraries(xfapi_main PRIVATE ${NVIPC_${_dep}_LIB})
        endif()
    endforeach()
    set_target_properties(xfapi_main PROPERTIES
        BUILD_RPATH "${NVIPC_DIR};${NVIPC_PARENT}"
        INSTALL_RPATH "${NVIPC_DIR};${NVIPC_PARENT}"
    )
endif()

set(LINK_LIBS "")

find_package(PkgConfig REQUIRED)

pkg_check_modules(YAML REQUIRED yaml-0.1)
if (YAML_FOUND)
    message(STATUS "Found libyaml.")
    target_include_directories(xfapi_main PRIVATE ${YAML_INCLUDE_DIRS})
    list(APPEND LINK_LIBS ${YAML_LIBRARIES})
else()
    message(FATAL_ERROR "libyaml not found. Please install libyaml-dev.")
endif()

list(APPEND LINK_LIBS z)
list(APPEND LINK_LIBS m)

if(OAI_OSC OR FLEXRAN_OSC OR FLEXRAN_OAI OR AERIAL_OSC OR OCUDU_OCUDU OR OAI_OCUDU OR AERIAL_OCUDU OR
   L1_SIM_OAI_OSC OR L1_SIM_FRAN_OSC OR L2_SIM_OAI_FRAN OR L2_SIM_OSC_FRAN)

    set(DPDK_PATH $ENV{DPDK_PATH})
    if(NOT DPDK_PATH)
        message(FATAL_ERROR "DPDK_PATH environment variable not set.")
    endif()
    message(STATUS "Using DPDK from: ${DPDK_PATH}")

    list(APPEND ENV{PKG_CONFIG_PATH} "${DPDK_PATH}/lib/x86_64-linux-gnu/pkgconfig")

    if(OCUDU_OCUDU OR OAI_OCUDU OR AERIAL_OCUDU)
        set(XSM_LIB_DIR  "${PROJECT_SOURCE_DIR}/src/ipc/xsm")
        set(XSM_LIB_PATH "${XSM_LIB_DIR}/libxsm.so")
        if(NOT EXISTS ${XSM_LIB_PATH})
            message(FATAL_ERROR "libxsm.so not found at ${XSM_LIB_PATH}.")
        endif()
        target_include_directories(xfapi_main PRIVATE ${XSM_LIB_DIR})
        list(APPEND LINK_LIBS ${XSM_LIB_PATH})
    endif()

    pkg_check_modules(DPDK REQUIRED libdpdk)
    if(NOT DPDK_FOUND)
        message(FATAL_ERROR "DPDK not found.")
    endif()
    message(STATUS "Found DPDK, version ${DPDK_VERSION}")
    target_include_directories(xfapi_main PRIVATE ${DPDK_INCLUDE_DIRS})
    list(APPEND LINK_LIBS ${DPDK_LIBRARIES})
    list(APPEND LINK_LIBS hugetlbfs)

endif()

set(GCC_WARNING_FLAGS -Wall -Wextra -Wpedantic)

target_compile_options(xfapi_main PRIVATE ${GCC_WARNING_FLAGS})

target_compile_options(xfapi_main PRIVATE
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    -fPIE
)

target_link_options(xfapi_main PRIVATE
    -pie
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
)

if(LINK_LIBS)
    message(STATUS "Final libraries to link: ${LINK_LIBS}")
    target_link_libraries(xfapi_main PRIVATE ${LINK_LIBS})
else()
    message(WARNING "No external libraries are being linked. Ensure this is correct for the chosen build mode.")
endif()

set_target_properties(xfapi_main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
