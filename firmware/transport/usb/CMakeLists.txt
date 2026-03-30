# PicoMSO USB Transport Backend
#
# This target provides the custom RP2040 USB device driver (picomso_usb_transport)
# together with the glue layer that adapts it to transport_interface_t.  It
# requires the Pico SDK to be initialised by the parent project before this
# subdirectory is added.
#
# File roles:
#   usb_common.h        - Generic USB standard definitions (constants, packed structs)
#   usb.h / usb.c       - Generic USB device driver (hardware register access, IRQ handler)
#   usb_config.h        - PicoMSO-specific USB descriptor and endpoint configuration
#   usb_config.c        - PicoMSO-specific USB device configuration instance
#                         (usb_config.c is #included by usb.c, not compiled separately)
#   usb_transport.h/.c  - Glue layer: adapts usb.h to transport_interface_t;
#                         provides control_transfer_handler and usb_transport_iface

add_library(picomso_usb_transport STATIC
    usb.c
    usb_transport.c
)

target_include_directories(picomso_usb_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Requires Pico SDK hardware libraries.  No TinyUSB dependency.
# Also links picomso_transport for the transport_interface_t definition.
target_link_libraries(picomso_usb_transport
    PUBLIC
        hardware_irq
        hardware_resets
        pico_stdlib
        picomso_transport
)
