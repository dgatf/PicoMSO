# PicoMSO firmware application
#
# Standalone Pico SDK project for the current PicoMSO firmware path.
#
# Build (from the repository root):
#
#   git submodule update --init --recursive
#   cmake -S firmware/app \
#         -B build/picomso
#   cmake --build build/picomso
#
# Produces:
#   build/picomso/picomso.elf
#   build/picomso/picomso.uf2

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

# ── Shared firmware libraries ────────────────────────────────────────────────
# Each library is included as an out-of-source subdirectory so that it can
# share the build directory tree with the main executable without conflicting
# with any other project build.

add_subdirectory(../common     ${CMAKE_BINARY_DIR}/picomso_common)
add_subdirectory(../mixed_signal ${CMAKE_BINARY_DIR}/picomso_mixed_signal)
add_subdirectory(../protocol   ${CMAKE_BINARY_DIR}/picomso_protocol)
add_subdirectory(../transport  ${CMAKE_BINARY_DIR}/picomso_transport)
add_subdirectory(../integration ${CMAKE_BINARY_DIR}/picomso_integration)
# ── Executable ───────────────────────────────────────────────────────────────

add_executable(${PROJECT_NAME}
    main.c
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    picomso_integration      # integration layer (transport-agnostic)
    picomso_usb_transport    # real USB backend; added by transport/CMakeLists.txt
                             # when PICO_SDK_INITIALIZED is set
)

pico_add_extra_outputs(${PROJECT_NAME})

# Disable stdio over USB CDC so that EP0 / the USB stack remain under the
# control of the custom USB driver used by picomso_usb_transport.
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
