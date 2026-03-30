cmake_minimum_required(VERSION 3.12)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(PicoMSOFirmware C CXX ASM)

pico_sdk_init()

add_subdirectory(common)
add_subdirectory(protocol)
add_subdirectory(transport)
add_subdirectory(integration)
add_subdirectory(mixed_signal)

