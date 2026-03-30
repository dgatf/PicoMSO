add_library(picomso_protocol STATIC
    src/protocol.c
    src/protocol_dispatch.c
)

target_include_directories(picomso_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The protocol layer is transport-agnostic and depends only on the C
# standard library (string.h / stdint.h).  No Pico SDK, USB, PIO, ADC,
# or DMA headers are used here.
