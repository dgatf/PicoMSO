add_library(picomso_integration STATIC
    src/integration.c
)

target_include_directories(picomso_integration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The integration layer depends on the protocol layer (for picomso_dispatch)
# and the transport layer (for transport_ctx_t and helpers).
# It does not depend directly on picomso_common; that dependency is pulled
# in transitively through picomso_protocol.
target_link_libraries(picomso_integration
    PUBLIC
        picomso_protocol
        picomso_transport
)
