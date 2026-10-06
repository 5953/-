// main.c - 主程序
#include "pico/stdlib.h"
#include "stm_programmer.h"
#include "oled_ui.h"

// 硬件初始化
void hardware_init(void) {
    stdio_init_all();  // 初始化标准输入输出
    
    // I2C初始化
    i2c_init(i2c0, 400000);
    gpio_set_function(PIN_OLED_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_OLED_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_OLED_SDA);
    gpio_pull_up(PIN_OLED_SCL);
    
    // 按键初始化 - 使用新的命名
    gpio_init(PIN_KEY_BACK);
    gpio_init(PIN_KEY_OK);
    gpio_init(PIN_KEY_DOWN);
    gpio_init(PIN_KEY_UP);
    gpio_set_dir(PIN_KEY_BACK, GPIO_IN);
    gpio_set_dir(PIN_KEY_OK, GPIO_IN);
    gpio_set_dir(PIN_KEY_DOWN, GPIO_IN);
    gpio_set_dir(PIN_KEY_UP, GPIO_IN);
    gpio_pull_up(PIN_KEY_BACK);
    gpio_pull_up(PIN_KEY_OK);
    gpio_pull_up(PIN_KEY_DOWN);
    gpio_pull_up(PIN_KEY_UP);
    
    // 烧录接口
    gpio_init(PIN_SWIM_SWDIO);
    gpio_init(PIN_SWCLK);
    gpio_init(PIN_NRST);
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    gpio_set_dir(PIN_SWCLK, GPIO_OUT);
    gpio_set_dir(PIN_NRST, GPIO_OUT);
    gpio_put(PIN_NRST, 1);
    gpio_put(PIN_SWIM_SWDIO, 1);
    gpio_put(PIN_SWCLK, 0);
}

// 系统初始化
static bool system_init(void) {
    // 使用新的显示系统
    if (!oled_ui_init()) {
        return false;
    }
    
    crc32_init();
    keys_init();
    chip_db_init();
    
    if (!config_load(&g_state.config)) {
        config_set_default(&g_state.config);
        config_save(&g_state.config);
    }
    
    if (!fs_init()) {
        return false;
    }
    
    fs_list_files(g_state.file_list, &g_state.file_count, MAX_FILES);
    menu_system_init();
    
    return true;
}

// 主循环
static void main_loop(void) {
    while (1) {
        key_event_t key = keys_scan();
        
        menu_system_process(&g_menu, key, &g_state);
        
        // 处理烧录进度
        if (g_menu.current_menu == MENU_PROGRAM_PROGRESS) {
            if (g_state.prog_state == PROG_STATE_IDLE) {
                int mode = (int)(intptr_t)g_menu.user_data;
                
                if (mode == 0) {
                    // 烧录模式
                    programmer_full_process(&g_state, 
                        g_state.file_list[g_menu.selected_item].filename);
                } else if (mode == 1) {
                    // 读取模式
                    g_state.prog_state = PROG_STATE_DETECTING;
                    strcpy(g_state.prog_message, "检测中...");
                    
                    if (programmer_auto_detect(&g_state)) {
                        g_state.prog_state = PROG_STATE_READING;
                        strcpy(g_state.prog_message, "读取中...");
                        
                        if (!g_state.read_buffer) {
                            g_state.read_buffer = malloc(g_state.detected_chip.flash_size);
                            g_state.read_buffer_size = g_state.detected_chip.flash_size;
                        }
                        
                        if (programmer_read_flash_stream(&g_state, g_state.read_buffer,
                                                        g_state.detected_chip.flash_addr,
                                                        g_state.detected_chip.flash_size)) {
                            char filename[MAX_FILENAME_LEN];
                            snprintf(filename, sizeof(filename), "READ_%s_%lu.BIN",
                                    g_state.detected_chip.name,
                                    to_ms_since_boot(get_absolute_time()));
                            
                            fs_write_file(filename, g_state.read_buffer,
                                        g_state.detected_chip.flash_size, FILE_TYPE_BIN);
                            
                            g_state.prog_state = PROG_STATE_SUCCESS;
                            strcpy(g_state.prog_message, "读取成功!");
                        } else {
                            g_state.prog_state = PROG_STATE_ERROR;
                            strcpy(g_state.prog_message, "读取失败");
                        }
                    } else {
                        g_state.prog_state = PROG_STATE_ERROR;
                        strcpy(g_state.prog_message, "检测失败");
                    }
                } else if (mode == 2) {
                    // 擦除模式
                    g_state.prog_state = PROG_STATE_DETECTING;
                    strcpy(g_state.prog_message, "检测中...");
                    
                    if (programmer_auto_detect(&g_state)) {
                        g_state.prog_state = PROG_STATE_ERASING;
                        strcpy(g_state.prog_message, "擦除中...");
                        
                        if (programmer_erase_chip(&g_state)) {
                            g_state.prog_state = PROG_STATE_SUCCESS;
                            strcpy(g_state.prog_message, "擦除成功!");
                        } else {
                            g_state.prog_state = PROG_STATE_ERROR;
                            strcpy(g_state.prog_message, "擦除失败");
                        }
                    } else {
                        g_state.prog_state = PROG_STATE_ERROR;
                        strcpy(g_state.prog_message, "检测失败");
                    }
                }
            }
        }
        
        // 定期检测芯片连接
        static uint32_t last_check = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_check > 1000) {
            chip_type_t type;
            uint16_t id;
            g_state.chip_connected = programmer_detect_chip(&type, &id);
            last_check = now;
        }
        
        sleep_ms(10);
    }
}

// 主函数
int main(void) {
    // 硬件初始化
    hardware_init();
    
    // 系统初始化
    if (!system_init()) {
        while (1) {
            sleep_ms(1000);
        }
    }
    
    // 主循环
    main_loop();
    
    return 0;
}
