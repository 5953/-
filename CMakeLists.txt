cmake_minimum_required(VERSION 3.13)

# 导入 Pico SDK
include(pico_sdk_import.cmake)

project(stm_programmer C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 初始化 Pico SDK
pico_sdk_init()

# 编译 main.c
add_executable(stm_programmer
    main.c
)

# 链接 Pico SDK 所需库
target_link_libraries(stm_programmer
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_clocks
)

# USB 输出
pico_enable_stdio_usb(stm_programmer 1)

# 不使用 UART 输出
pico_enable_stdio_uart(stm_programmer 0)

# 生成 UF2、BIN、HEX 等文件
pico_add_extra_outputs(stm_programmer)
