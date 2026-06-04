cmake_minimum_required(VERSION 3.15)
project(xFAPI C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_C_FLAGS_DEBUG "-g")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ================================
# Compile-time L1+L2 combinations & Simulation Mode Macros
# ================================
option(OAI_OSC "Use OAI as L1 and OSC as L2" OFF)
option(FLEXRAN_OSC "Use FlexRAN as L1 and OSC as L2" OFF)
option(FLEXRAN_OAI "Use FlexRAN as L1 and OAI as L2" OFF)
option(AERIAL_OSC "Use Aerial as L1 and OSC as L2" OFF)
option(OCUDU_OCUDU "Use OCUDU as L1 and OCUDU as L2 (xSM-bridged)" OFF)
option(OAI_OCUDU "Use OAI as L1 (nFAPI/UDP) and OCUDU as L2 (xSM-bridged)" OFF)
option(L1_SIM_OAI_OSC "Simulated L1: OAI, L2: OSC" OFF)
option(L1_SIM_OAI_OAI "Simulated L1: OAI, L2: OAI" OFF)
option(L1_SIM_FRAN_OSC "Simulated L1: FlexRAN, L2: OSC" OFF)
option(L1_SIM_FRAN_OAI "Simulated L1: FlexRAN, L2: OAI" OFF)
option(L2_SIM_OAI_OAI "Simulated L2: OAI, L1: OAI" OFF)
option(L2_SIM_OAI_FRAN "Simulated L2: OAI, L1: FlexRAN" OFF)
option(L2_SIM_OSC_OAI "Simulated L2: OSC, L1: OAI" OFF)
option(L2_SIM_OSC_FRAN "Simulated L2: OSC, L1: FlexRAN" OFF)

# Define L1+L2 combinations
foreach(mode OAI_OSC FLEXRAN_OSC FLEXRAN_OAI AERIAL_OSC OCUDU_OCUDU OAI_OCUDU
              L1_SIM_OAI_OSC L1_SIM_OAI_OAI L1_SIM_FRAN_OSC L1_SIM_FRAN_OAI
              L2_SIM_OAI_OAI L2_SIM_OAI_FRAN L2_SIM_OSC_OAI L2_SIM_OSC_FRAN)
    if(${mode})
        add_compile_definitions(${mode})
        message(STATUS "Enabled compile definition: ${mode}")
    endif()
endforeach()

# ========================
# Add source files
# ========================
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
# conditional sources according to the selected mode

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


# OCUDU L1 + OCUDU L2 bridged via prebuilt libxsm.so.
# XFAPI runs as a single process: DPDK PRIMARY on file-prefix gnb0,
# xSM SLAVE on two memzones (one per OCUDU side). OCUDU sides are
# DPDK SECONDARY + xSM MASTER on their respective memzones.
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
        # Pulled in because main.c unconditionally references them.
        src/utils/config/yaml_fail_check.c
        src/utils/stats_generation/message_stats.c
    )
endif()


# OAI L1 (nFAPI over UDP sockets) + OCUDU L2 (xSM-bridged).
# XFAPI owns the "xsm_bridge" memzone (the role OCUDU-L1 plays in
# OCUDU_OCUDU): DPDK PRIMARY on file-prefix, xSM SLAVE on pair 1 toward
# OCUDU-L2 (which attaches as xSM MASTER on pair 1). The L1-facing side is
# UDP sockets to OAI instead of an xSM pair.
if(OAI_OCUDU)
    message(STATUS "Adding sources for OAI_OCUDU mode")

    list(APPEND SOURCES
        src/framework/dpdk/dpdk_init.c
        src/framework/oai_l1_interface.c
        src/framework/ocudu_l2_interface.c
        src/core/OAI_OCUDU/oai_vnf.c
        src/core/OAI_OCUDU/oai_p7.c
        src/core/OAI_OCUDU/forwarder_l2_to_oai.c
        # Pulled in because main.c unconditionally references them.
        src/utils/config/yaml_fail_check.c
        src/utils/stats_generation/message_stats.c
    )

    # -------------------------------------------------------------------
    # Third-party nFAPI codec (open-nFAPI). Built as a separate shared
    # library so the upstream sources stay 100% untouched — xfapi_main
    # links it and includes its public headers. Sources, include roots and
    # the two required defines were determined empirically; do NOT edit any
    # file under src/ipc/nfapi/.
    #   -DMAX_NUM_CCs=1  : OAI normally supplies this from PHY headers; we
    #                      pin it to 1 (single CC) for the standalone codec.
    #   -DENABLE_SOCKET  : selects the socket (SCTP P5 / UDP P7) transport
    #                      path in nfapi_p7.c (vs WLS).
    # -------------------------------------------------------------------
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
        # OAI support stub providing exit_function() referenced by the codec's
        # AssertFatal macro (from OAI's assertions.h). OAI's own file; bundled
        # so the codec .so is self-contained.
        ${NFAPI_OAICOM}/minimal_stub.c
    )

    add_library(nfapi_codec SHARED ${NFAPI_CODEC_SOURCES})
    target_include_directories(nfapi_codec PUBLIC
        ${NFAPI_DIR}                         # resolves "nfapi/oai_integration/..." + "nfapi/open-nFAPI/..."
        ${NFAPI_UP}/nfapi/public_inc
        ${NFAPI_UP}/common/public_inc
        ${NFAPI_UP}/fapi/inc
        ${NFAPI_OAICOM}                      # OAI common/platform_types.h
        ${NFAPI_OAICOM}/common/utils         # bare "assertions.h" etc.
    )
    # src/ipc is the include root used by the path-qualified upstream
    # includes ("nfapi/oai_integration/vendor_ext.h", "nfapi/open-nFAPI/...").
    target_include_directories(nfapi_codec PRIVATE ${PROJECT_SOURCE_DIR}/src/ipc)
    target_compile_definitions(nfapi_codec PUBLIC MAX_NUM_CCs=1 ENABLE_SOCKET)
    # Upstream code is third-party: silence its warnings so our -Werror-free
    # build stays clean without editing it.
    target_compile_options(nfapi_codec PRIVATE -w)
    target_link_libraries(nfapi_codec PUBLIC z pthread m)
endif()





add_executable(xfapi_main ${SOURCES})

# Add include directories to the target
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

# OAI_OCUDU: expose the third-party nFAPI codec's public headers to our code
# and link the codec shared library into xfapi_main. Our own nFAPI/VNF glue
# (written under src/core/OAI_OCUDU/) includes these public headers and calls
# the codec's pack/unpack entry points; it never includes upstream .c files.
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
    )
    # Our glue includes the same public headers, so it must see the same
    # MAX_NUM_CCs / ENABLE_SOCKET definitions the codec was built with.
    target_compile_definitions(xfapi_main PRIVATE MAX_NUM_CCs=1 ENABLE_SOCKET)
    # nfapi_codec: the third-party nFAPI codec. sctp: P5 control plane runs
    # over SCTP (xFAPI is the VNF/server).
    target_link_libraries(xfapi_main PRIVATE nfapi_codec sctp)
endif()

# ===========================
# Initialize Linker Dependencies
# ===========================
set(LINK_LIBS "")

# ===========================
# Core Dependencies (Always On)
# ===========================
find_package(PkgConfig REQUIRED)

# --- YAML dependency (libyaml) ---
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

if(OAI_OSC OR FLEXRAN_OSC OR FLEXRAN_OAI OR AERIAL_OSC OR OCUDU_OCUDU OR OAI_OCUDU OR
   L1_SIM_OAI_OSC OR L1_SIM_FRAN_OSC OR L2_SIM_OAI_FRAN OR L2_SIM_OSC_FRAN)

    set(DPDK_PATH $ENV{DPDK_PATH})
    if(NOT DPDK_PATH)
        message(FATAL_ERROR "DPDK_PATH environment variable not set.")
    endif()
    message(STATUS "Using DPDK from: ${DPDK_PATH}")

    list(APPEND ENV{PKG_CONFIG_PATH} "${DPDK_PATH}/lib/x86_64-linux-gnu/pkgconfig")

    if(OCUDU_OCUDU OR OAI_OCUDU)
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

# ===============================================
# Apply Compiler Warnings to the Target
# ===============================================
# Add this line here. It applies the flags you just defined
# to the 'xfapi_main' target. This assumes add_executable(xfapi_main ...)
# has already been called earlier in your script.
target_compile_options(xfapi_main PRIVATE ${GCC_WARNING_FLAGS})

# Standard runtime security flags. These protect users from common C
# vulnerabilities (stack-buffer overflows, libc misuse, RELRO/PLT
# overwrites) and are recommended for any shipped C binary. Match the
# Debian/Ubuntu hardening-includes defaults.
target_compile_options(xfapi_main PRIVATE
    -fstack-protector-strong       # canary on functions that take addresses or large buffers
    -D_FORTIFY_SOURCE=2            # compile- and run-time checks for memcpy/strcpy/printf/...
    -fPIE                          # position-independent code (paired with -pie at link time)
)

target_link_options(xfapi_main PRIVATE
    -pie                           # PIE executable; ASLR for the main image
    -Wl,-z,relro                   # mark relocations read-only after dynamic linking
    -Wl,-z,now                     # resolve all symbols at startup (full RELRO)
    -Wl,-z,noexecstack             # make stack non-executable
)

# ===========================
# Final Linking Stage
# ===========================
if(LINK_LIBS)
    message(STATUS "Final libraries to link: ${LINK_LIBS}")
    target_link_libraries(xfapi_main PRIVATE ${LINK_LIBS})
else()
    message(WARNING "No external libraries are being linked. Ensure this is correct for the chosen build mode.")
endif()

# ===========================
# Set final executable location
# ===========================
set_target_properties(xfapi_main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
