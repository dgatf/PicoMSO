add_library(picomso_transport STATIC
    src/transport.c
)

target_include_directories(picomso_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The transport layer is a pure-C abstraction with no hardware dependency.
# It does not link pico_stdlib, TinyUSB, or any peripheral library.
# Concrete backends will be separate targets that link picomso_transport.

# ---------------------------------------------------------------------------
# USB CDC transport backend
#
# This concrete backend is only available when the TinyUSB device stack
# target exists (provided by the Pico SDK).  It is intentionally NOT built
# as part of the standalone firmware/ scaffolding so that firmware/ remains
# free of SDK and hardware dependencies.
#
# Projects that need the USB transport (e.g. logic_analyzer_rp2040,
# oscilloscope_rp2040) should link picomso_usb_transport after enabling
# TinyUSB in their own CMakeLists.txt.
# ---------------------------------------------------------------------------
if(TARGET tinyusb_device)
    add_library(picomso_usb_transport STATIC
        src/usb_transport.c
    )

    target_include_directories(picomso_usb_transport
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # picomso_transport is PUBLIC so that callers of picomso_usb_transport
    # automatically get the transport abstraction headers and helpers.
    # tinyusb_device is PRIVATE: its details must not leak into callers.
    target_link_libraries(picomso_usb_transport
        PUBLIC
            picomso_transport
        PRIVATE
            tinyusb_device
    )
endif()
