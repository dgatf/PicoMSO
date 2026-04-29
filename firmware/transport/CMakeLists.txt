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

# USB transport backend (requires Pico SDK; skip it when this directory is
# consumed without the Pico SDK having been initialized by a parent project).
if(TARGET pico_stdlib)
    add_subdirectory(usb)
endif()
