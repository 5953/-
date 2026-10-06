#include "chip_database.h"
#include <string.h>

// 全局芯片数据库
static chip_info_t g_chip_db[MAX_CHIP_DB] = {0};
static int g_chip_db_count = 0;

void chip_db_init(void) {
    g_chip_db_count = 0;
    
    // STM8系列芯片
    chip_info_t stm8_chips[] = {
        {"STM8S003F3", 0x5344, 0xFFFF, CHIP_TYPE_STM8, 0x8000, 8*1024, 1024, 64, 64, true},
        {"STM8S103F3", 0x5348, 0xFFFF, CHIP_TYPE_STM8, 0x8000, 8*1024, 1024, 64, 64, true},
        // 在这里添加更多STM8芯片
    };
    
    // STM32系列芯片
    chip_info_t stm32_chips[] = {
        {"STM32F030F4", 0x0444, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 16*1024, 4*1024, 1024, 1024, true},
        {"STM32F103C8", 0x0410, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 64*1024, 20*1024, 1024, 1024, true},
        {"STM32F103CB", 0x0410, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 128*1024, 20*1024, 1024, 1024, true},
        // 在这里添加更多STM32芯片
    };
    
    // 复制STM8芯片到全局数据库
    for (int i = 0; i < sizeof(stm8_chips)/sizeof(stm8_chips[0]); i++) {
        if (g_chip_db_count < MAX_CHIP_DB) {
            g_chip_db[g_chip_db_count++] = stm8_chips[i];
        }
    }
    
    // 复制STM32芯片到全局数据库
    for (int i = 0; i < sizeof(stm32_chips)/sizeof(stm32_chips[0]); i++) {
        if (g_chip_db_count < MAX_CHIP_DB) {
            g_chip_db[g_chip_db_count++] = stm32_chips[i];
        }
    }
}

chip_info_t* chip_db_find_by_id(uint16_t device_id, chip_type_t type) {
    for (int i = 0; i < g_chip_db_count; i++) {
        if (g_chip_db[i].type == type && g_chip_db[i].valid) {
            if ((device_id & g_chip_db[i].id_mask) == g_chip_db[i].device_id) {
                return &g_chip_db[i];
            }
        }
    }
    return NULL;
}

int chip_db_get_count(void) {
    return g_chip_db_count;
}

chip_info_t* chip_db_get_all(void) {
    return g_chip_db;
}

const char* chip_db_get_name_by_id(uint16_t device_id, chip_type_t type) {
    chip_info_t* chip = chip_db_find_by_id(device_id, type);
    return chip ? chip->name : NULL;
}

uint32_t chip_db_get_flash_size_by_id(uint16_t device_id, chip_type_t type) {
    chip_info_t* chip = chip_db_find_by_id(device_id, type);
    return chip ? chip->flash_size : 0;
}