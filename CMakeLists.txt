cmake_minimum_required(VERSION 3.13)

# 设置 Pico SDK 路径
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk)

# 导入 Pico SDK
include(pico_sdk_import.cmake)

project(stm_programmer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 初始化 Pico SDK
pico_sdk_init()

add_executable(stm_programmer
    main.c
    protocol_and_menu.c
    oled_ui.c
    font_ascii.c
    font_chinese.c
    chip_database.c
)

target_link_libraries(stm_programmer
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_clocks
    pico_binary_info
)

pico_enable_stdio_usb(stm_programmer 1)
pico_enable_stdio_uart(stm_programmer 0)
pico_add_extra_outputs(stm_programmer)
