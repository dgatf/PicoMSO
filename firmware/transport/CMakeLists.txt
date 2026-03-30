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

# USB transport backend (requires Pico SDK; not built when firmware/ is
# used as a pure scaffolding include without a Pico SDK toolchain).
#if(PICO_SDK_INITIALIZED)
add_subdirectory(usb)
#endif()
