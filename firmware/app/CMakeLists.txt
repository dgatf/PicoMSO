# PicoMSO firmware application
#
# Standalone Pico SDK project for the current PicoMSO firmware path.
# Supports RP2040 and RP2350 builds.
#
# Build (from the repository root):
#
#   git submodule update --init --recursive
#
#   # RP2040
#   cmake -S firmware/app \
#         -B build/rp2040
#   cmake --build build/rp2040
#
#   # RP2350
#   cmake -S firmware/app \
#         -B build/rp2350 \
#         -DPICO_BOARD=pico2
#   cmake --build build/rp2350
#
# Produces:
#   build/rp2040/picomso.elf
#   build/rp2040/picomso.uf2
#   build/rp2350/picomso.elf
#   build/rp2350/picomso.uf2

cmake_minimum_required(VERSION 3.12)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../external/pico-sdk")

if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR
        "Pico SDK submodule not found.\n"
        "\n"
        "Expected at:\n"
        "  ${PICO_SDK_PATH}\n"
        "\n"
        "Please run:\n"
        "  git submodule update --init --recursive\n")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(picomso C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

execute_process(
    COMMAND git describe --tags
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../..
    OUTPUT_VARIABLE PROJECT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Remove optional leading "v"
string(REGEX REPLACE "^v" "" PROJECT_VERSION_CLEAN "${PROJECT_VERSION}")

# Extract version components
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${PROJECT_VERSION_CLEAN}")

if(CMAKE_MATCH_1)
    set(PICOMSO_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(PICOMSO_VERSION_MINOR ${CMAKE_MATCH_2})
    set(PICOMSO_VERSION_PATCH ${CMAKE_MATCH_3})
else()
    set(PICOMSO_VERSION_MAJOR 0)
    set(PICOMSO_VERSION_MINOR 0)
    set(PICOMSO_VERSION_PATCH 0)
endif()

add_compile_definitions(
    PICOMSO_VERSION_MAJOR=${PICOMSO_VERSION_MAJOR}
    PICOMSO_VERSION_MINOR=${PICOMSO_VERSION_MINOR}
    PICOMSO_VERSION_PATCH=${PICOMSO_VERSION_PATCH}
)

# ── Shared firmware libraries ────────────────────────────────────────────────
# Each library is included as an out-of-source subdirectory so that it can
# share the build directory tree with the main executable without conflicting
# with any other project build.

add_subdirectory(../common       ${CMAKE_BINARY_DIR}/picomso_common)
add_subdirectory(../mixed_signal ${CMAKE_BINARY_DIR}/picomso_mixed_signal)
add_subdirectory(../protocol     ${CMAKE_BINARY_DIR}/picomso_protocol)
add_subdirectory(../transport    ${CMAKE_BINARY_DIR}/picomso_transport)
add_subdirectory(../integration  ${CMAKE_BINARY_DIR}/picomso_integration)

# ── Executable ───────────────────────────────────────────────────────────────

add_executable(${PROJECT_NAME}
    main.c
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    picomso_integration
    picomso_usb_transport
)

pico_add_extra_outputs(${PROJECT_NAME})

# Disable stdio over USB CDC so that EP0 / the USB stack remain under the
# control of the custom USB driver used by picomso_usb_transport.
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)