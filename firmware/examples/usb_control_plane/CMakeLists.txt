# PicoMSO USB control-plane example
#
# Standalone Pico SDK project that demonstrates connecting the USB
# transport backend to the integration layer.
#
# Prerequisites:
#   - Pico SDK installed; PICO_SDK_PATH environment variable set.
#
# Build (from the repository root):
#
#   PICO_SDK_PATH=/path/to/pico-sdk \
#     cmake -S firmware/examples/usb_control_plane \
#           -B build/usb_control_plane
#   cmake --build build/usb_control_plane
#
# Produces:
#   build/usb_control_plane/picomso_usb_control_plane.elf
#   build/usb_control_plane/picomso_usb_control_plane.uf2

cmake_minimum_required(VERSION 3.12)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picomso_usb_control_plane C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ── Shared firmware libraries ────────────────────────────────────────────────
# Each library is included as an out-of-source subdirectory so that it can
# share the build directory tree with the main executable without conflicting
# with any other project build.

add_subdirectory(../../common     ${CMAKE_BINARY_DIR}/picomso_common)
add_subdirectory(../../mixed_signal ${CMAKE_BINARY_DIR}/picomso_mixed_signal)
add_subdirectory(../../protocol   ${CMAKE_BINARY_DIR}/picomso_protocol)
add_subdirectory(../../transport  ${CMAKE_BINARY_DIR}/picomso_transport)
add_subdirectory(../../integration ${CMAKE_BINARY_DIR}/picomso_integration)

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
