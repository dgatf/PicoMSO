cmake_minimum_required(VERSION 3.16)
project(picomso_sigrok_driver LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(picomso_sigrok_driver STATIC
    api.c
    protocol.c
)

target_include_directories(picomso_sigrok_driver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../firmware/protocol/include
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(picomso_sigrok_driver PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()

add_executable(picomso_sigrok_driver_test
    tests/test_driver.c
)

target_link_libraries(picomso_sigrok_driver_test PRIVATE picomso_sigrok_driver)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(picomso_sigrok_driver_test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME picomso_sigrok_driver_test COMMAND picomso_sigrok_driver_test)
