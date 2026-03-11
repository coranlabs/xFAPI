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
option(L1_SIM_OAI_OSC "Simulated L1: OAI, L2: OSC" OFF)
option(L1_SIM_OAI_OAI "Simulated L1: OAI, L2: OAI" OFF)
option(L1_SIM_FRAN_OSC "Simulated L1: FlexRAN, L2: OSC" OFF)
option(L1_SIM_FRAN_OAI "Simulated L1: FlexRAN, L2: OAI" OFF)
option(L2_SIM_OAI_OAI "Simulated L2: OAI, L1: OAI" OFF)
option(L2_SIM_OAI_FRAN "Simulated L2: OAI, L1: FlexRAN" OFF)
option(L2_SIM_OSC_OAI "Simulated L2: OSC, L1: OAI" OFF)
option(L2_SIM_OSC_FRAN "Simulated L2: OSC, L1: FlexRAN" OFF)

# Define L1+L2 combinations
foreach(mode OAI_OSC FLEXRAN_OSC FLEXRAN_OAI AERIAL_OSC OCUDU_OCUDU
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

if(OAI_OSC OR FLEXRAN_OSC OR FLEXRAN_OAI OR AERIAL_OSC OR OCUDU_OCUDU OR
   L1_SIM_OAI_OSC OR L1_SIM_FRAN_OSC OR L2_SIM_OAI_FRAN OR L2_SIM_OSC_FRAN)

    set(DPDK_PATH $ENV{DPDK_PATH})
    if(NOT DPDK_PATH)
        message(FATAL_ERROR "DPDK_PATH environment variable not set.")
    endif()
    message(STATUS "Using DPDK from: ${DPDK_PATH}")

    list(APPEND ENV{PKG_CONFIG_PATH} "${DPDK_PATH}/lib/x86_64-linux-gnu/pkgconfig")

    if(OCUDU_OCUDU)
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
