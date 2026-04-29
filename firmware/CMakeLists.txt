cmake_minimum_required(VERSION 3.12)

project(PicoMSOFirmware LANGUAGES C CXX)

add_subdirectory(common)
add_subdirectory(mixed_signal)
add_subdirectory(protocol)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../external/usb_library_rp2040
    usb_library_rp2040
)
add_subdirectory(transport)
add_subdirectory(integration)
