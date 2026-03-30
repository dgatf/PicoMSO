add_library(picomso_protocol STATIC
    src/protocol.c
    src/protocol_dispatch.c
)

target_include_directories(picomso_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The protocol layer is transport-agnostic: no USB, PIO, ADC, or DMA
# dependency.  It links picomso_common solely to access capture_controller,
# which is a hardware-free value object tracking mode and capture state.
target_link_libraries(picomso_protocol
    PUBLIC
        picomso_common
)
