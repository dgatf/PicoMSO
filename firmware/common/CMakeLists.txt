add_library(picomso_common STATIC
    src/debug.c
)

target_include_directories(picomso_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(picomso_common PUBLIC
    pico_stdlib
    hardware_uart
    hardware_gpio
)

