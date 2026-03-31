cmake_minimum_required(VERSION 3.16)
project(picomso_sigrok_driver C)

set(PICOMSO_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

add_library(picomso_sigrok_driver STATIC
    api.c
    protocol.c
)

target_compile_features(picomso_sigrok_driver PUBLIC c_std_99)

target_include_directories(picomso_sigrok_driver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PICOMSO_REPO_ROOT}
)

add_executable(picomso_sigrok_driver_test
    test_driver.c
)

target_compile_features(picomso_sigrok_driver_test PRIVATE c_std_99)

target_link_libraries(picomso_sigrok_driver_test
    PRIVATE
        picomso_sigrok_driver
)

enable_testing()
add_test(NAME picomso_sigrok_driver_test COMMAND picomso_sigrok_driver_test)
