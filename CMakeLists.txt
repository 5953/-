cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(stm_programmer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(stm_programmer
    main.c
    protocol_and_menu.c
    oled_ui.c
    font_ascii.c
    font_chinese.c
    chip_database.c
)

# 添加所有必要的库
target_link_libraries(stm_programmer
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_clocks
    hardware_resets
    hardware_pwm
    hardware_dma
    pico_binary_info
    pico_bootrom
    pico_flash
    pico_time
    pico_malloc
    pico_printf
    pico_runtime
    pico_sync
)

# 设置 Pico 目标属性
pico_set_binary_name(stm_programmer)
pico_enable_stdio_usb(stm_programmer 1)
pico_enable_stdio_uart(stm_programmer 0)
pico_add_extra_outputs(stm_programmer)
