cmake_minimum_required(VERSION 3.15)
project(xFAPI C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BRIDGE_MODE "Build xFAPI bridge mode" OFF)

if(BRIDGE_MODE)
    add_compile_definitions(BRIDGE_MODE)
    message(STATUS "Enabled compile definition: BRIDGE_MODE")
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

set(LINK_LIBS "")
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML REQUIRED yaml-0.1)
if(YAML_FOUND)
    message(STATUS "Found libyaml.")
    target_include_directories(xfapi_main PRIVATE ${YAML_INCLUDE_DIRS})
    list(APPEND LINK_LIBS ${YAML_LIBRARIES})
else()
    message(FATAL_ERROR "libyaml not found.")
endif()
list(APPEND LINK_LIBS z m)

set(GCC_WARNING_FLAGS -Wall -Wextra -Wpedantic)
target_compile_options(xfapi_main PRIVATE ${GCC_WARNING_FLAGS}
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE)
target_link_options(xfapi_main PRIVATE
    -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)

if(LINK_LIBS)
    target_link_libraries(xfapi_main PRIVATE ${LINK_LIBS})
endif()

set_target_properties(xfapi_main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
