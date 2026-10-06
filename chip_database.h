#ifndef CHIP_DATABASE_H
#define CHIP_DATABASE_H

#include "stm_programmer.h"

// 芯片数据库函数声明
void chip_db_init(void);
chip_info_t* chip_db_find_by_id(uint16_t device_id, chip_type_t type);
int chip_db_get_count(void);
chip_info_t* chip_db_get_all(void);

// 芯片数据访问函数
const char* chip_db_get_name_by_id(uint16_t device_id, chip_type_t type);
uint32_t chip_db_get_flash_size_by_id(uint16_t device_id, chip_type_t type);

#endif // CHIP_DATABASE_H