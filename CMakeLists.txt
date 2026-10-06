cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(stm_programmer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(stm_programmer
    main.c                    # 改这里
    font_data.c
    protocol_and_menu.c
)

pico_enable_stdio_usb(stm_programmer 1)
pico_enable_stdio_uart(stm_programmer 0)

pico_add_extra_outputs(stm_programmer)

target_link_libraries(stm_programmer
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_clocks
)
