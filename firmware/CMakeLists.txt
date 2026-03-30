cmake_minimum_required(VERSION 3.12)

project(PicoMSOFirmware LANGUAGES C CXX)

add_subdirectory(common)
add_subdirectory(protocol)
add_subdirectory(transport)
add_subdirectory(mixed_signal)

