cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(RP2040_Test C CXX ASM)

pico_sdk_init()

add_executable(RP2040_Test
    main.c
)

target_link_libraries(RP2040_Test
    pico_stdlib
)

pico_add_extra_outputs(RP2040_Test)
