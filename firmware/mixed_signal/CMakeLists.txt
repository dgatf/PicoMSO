add_library(picomso_firmware_mixed_signal STATIC
    src/logic_capture.c
    src/scope_capture.c
    src/mixed_capture.c
)

target_include_directories(picomso_firmware_mixed_signal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(picomso_firmware_mixed_signal
    PUBLIC
        pico_stdlib
        hardware_adc
        hardware_gpio
        picomso_common
        hardware_pio
        hardware_dma
)

pico_generate_pio_header(picomso_firmware_mixed_signal ${CMAKE_CURRENT_LIST_DIR}/src/logic_capture.pio)
pico_generate_pio_header(picomso_firmware_mixed_signal ${CMAKE_CURRENT_LIST_DIR}/src/scope_trigger.pio)