// protocol_and_menu.c - 协议实现和完整菜单系统
// 接续 stm_programmer_main.c
#include "stm_programmer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// 从main.c导入的外部声明
extern uint8_t g_oled_buffer[];
extern chip_info_t g_chip_db[];
extern int g_chip_db_count;
extern programmer_state_t g_state;
extern menu_state_t g_menu;
extern uint8_t g_stream_buffer[];

// 从main.c导入的函数声明
extern void oled_clear(void);
extern void oled_refresh(void);
extern void oled_show_string(int x, int y, const char *str);
extern void oled_show_number(int x, int y, uint32_t num);
extern void oled_show_progress(int x, int y, int w, int h, uint32_t value, uint32_t total);
extern void oled_show_percent(int x, int y, uint32_t value, uint32_t total);
extern void oled_show_speed(int x, int y, uint32_t bytes_per_sec);
extern void oled_draw_hline(int x, int y, int w);
extern void oled_draw_rect(int x, int y, int w, int h, bool fill);
//=============================================================================
// 芯片数据库初始化
//=============================================================================

static void chip_db_init(void) {
    g_chip_db_count = 0;
    
    // STM8系列
    chip_info_t stm8_chips[] = {
        {"STM8S003F3", 0x5344, 0xFFFF, CHIP_TYPE_STM8, 0x8000, 8*1024, 1024, 64, 64, true},
        {"STM8S103F3", 0x5348, 0xFFFF, CHIP_TYPE_STM8, 0x8000, 8*1024, 1024, 64, 64, true},
        {"STM8S105K4", 0x5358, 0xFFFF, CHIP_TYPE_STM8, 0x8000, 16*1024, 2048, 128, 128, true},
        {"STM8S207", 0x5378, 0xFFFF, CHIP_TYPE_STM8, 0x8000, 128*1024, 6144, 128, 128, true},
        {"STM8L151C6", 0x5367, 0xFFFF, CHIP_TYPE_STM8, 0x8000, 32*1024, 2048, 256, 256, true},
    };
    
    for (int i = 0; i < sizeof(stm8_chips)/sizeof(stm8_chips[0]); i++) {
        g_chip_db[g_chip_db_count++] = stm8_chips[i];
    }
    
    // STM32系列
    chip_info_t stm32_chips[] = {
        {"STM32F030F4", 0x0444, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 16*1024, 4*1024, 1024, 1024, true},
        {"STM32F103C6", 0x0410, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 32*1024, 10*1024, 1024, 1024, true},
        {"STM32F103C8", 0x0410, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 64*1024, 20*1024, 1024, 1024, true},
        {"STM32F103CB", 0x0410, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 128*1024, 20*1024, 1024, 1024, true},
        {"STM32F401CC", 0x0423, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 256*1024, 64*1024, 16384, 16384, true},
        {"STM32F405RG", 0x0413, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 1024*1024, 192*1024, 16384, 16384, true},
        {"STM32G030F6", 0x0466, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 32*1024, 8*1024, 2048, 2048, true},
        {"STM32L031K6", 0x0425, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 32*1024, 8*1024, 128, 128, true},
        {"STM32L151C6", 0x0416, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 32*1024, 10*1024, 256, 256, true},
        {"STM32F407VE", 0x0413, 0x0FFF, CHIP_TYPE_STM32, 0x08000000, 512*1024, 192*1024, 16384, 16384, true},
    };
    
    for (int i = 0; i < sizeof(stm32_chips)/sizeof(stm32_chips[0]); i++) {
        g_chip_db[g_chip_db_count++] = stm32_chips[i];
    }
}

static chip_info_t* chip_db_find_by_id(uint16_t device_id, chip_type_t type) {
    for (int i = 0; i < g_chip_db_count; i++) {
        if (g_chip_db[i].type == type && g_chip_db[i].valid) {
            if ((device_id & g_chip_db[i].id_mask) == g_chip_db[i].device_id) {
                return &g_chip_db[i];
            }
        }
    }
    return NULL;
}

//=============================================================================
// SWIM协议实现（STM8）
//=============================================================================

#define SWIM_CSR        0x7F80
#define SWIM_CSR2       0x7F81

static inline void swim_delay_us(uint32_t us) { sleep_us(us); }
static inline void swim_high(void) { gpio_put(PIN_SWIM_SWDIO, 1); }
static inline void swim_low(void) { gpio_put(PIN_SWIM_SWDIO, 0); }
static inline bool swim_read(void) { return gpio_get(PIN_SWIM_SWDIO); }
static inline void swim_output(void) { gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT); }
static inline void swim_input(void) { gpio_set_dir(PIN_SWIM_SWDIO, GPIO_IN); }

static bool swim_entry_sequence(void) {
    gpio_put(PIN_NRST, 0);
    swim_output();
    swim_high();
    sleep_us(100);
    
    gpio_put(PIN_NRST, 1);
    sleep_us(100);
    
    for (int i = 0; i < 4; i++) {
        swim_low();
        swim_delay_us(500);
        swim_high();
        swim_delay_us(500);
    }
    
    sleep_us(100);
    return true;
}

static void swim_write_bit(bool bit) {
    swim_output();
    if (bit) {
        swim_low();
        swim_delay_us(2);
        swim_high();
        swim_delay_us(2);
    } else {
        swim_high();
        swim_delay_us(2);
        swim_low();
        swim_delay_us(2);
    }
}

static bool swim_read_bit(void) {
    swim_input();
    swim_delay_us(2);
    bool first = swim_read();
    swim_delay_us(2);
    bool second = swim_read();
    return (!first && second);
}

static bool swim_write_byte(uint8_t data) {
    swim_write_bit(0);
    for (int i = 7; i >= 0; i--) {
        swim_write_bit((data >> i) & 1);
    }
    uint8_t parity = data;
    parity ^= parity >> 4;
    parity ^= parity >> 2;
    parity ^= parity >> 1;
    swim_write_bit(parity & 1);
    swim_input();
    swim_delay_us(10);
    return true;
}

static bool swim_read_byte(uint8_t *data) {
    swim_input();
    int timeout = 1000;
    while (swim_read_bit() && timeout--) {
        if (timeout <= 0) return false;
    }
    uint8_t value = 0;
    for (int i = 7; i >= 0; i--) {
        if (swim_read_bit()) {
            value |= (1 << i);
        }
    }
    swim_read_bit();
    swim_output();
    swim_write_bit(1);
    *data = value;
    return true;
}

static bool swim_read_memory(uint32_t addr, uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        swim_write_byte(0x01);
        swim_write_byte((addr >> 16) & 0xFF);
        swim_write_byte((addr >> 8) & 0xFF);
        swim_write_byte(addr & 0xFF);
        if (!swim_read_byte(&data[i])) {
            return false;
        }
        addr++;
    }
    return true;
}

static bool swim_write_memory(uint32_t addr, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        swim_write_byte(0x02);
        swim_write_byte((addr >> 16) & 0xFF);
        swim_write_byte((addr >> 8) & 0xFF);
        swim_write_byte(addr & 0xFF);
        swim_write_byte(data[i]);
        addr++;
    }
    return true;
}

static bool swim_init(void) {
    if (!swim_entry_sequence()) {
        return false;
    }
    uint8_t csr;
    if (!swim_read_memory(SWIM_CSR, &csr, 1)) {
        return false;
    }
    return true;
}

static bool swim_read_device_id(uint16_t *device_id) {
    if (!swim_init()) {
        return false;
    }
    uint8_t id[2];
    if (!swim_read_memory(0x004850, id, 2)) {
        return false;
    }
    *device_id = (id[0] << 8) | id[1];
    if (*device_id == 0x0000 || *device_id == 0xFFFF) {
        return false;
    }
    return true;
}

static bool swim_unlock_flash(void) {
    uint8_t key1 = 0x56;
    uint8_t key2 = 0xAE;
    swim_write_memory(0x5062, &key1, 1);
    swim_write_memory(0x5062, &key2, 1);
    sleep_ms(10);
    return true;
}

static bool swim_wait_flash_ready(uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    uint8_t iapsr;
    while (to_ms_since_boot(get_absolute_time()) - start < timeout_ms) {
        if (!swim_read_memory(0x505F, &iapsr, 1)) {
            return false;
        }
        if ((iapsr & 0x04) == 0) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

static bool swim_erase_chip(void) {
    if (!swim_unlock_flash()) {
        return false;
    }
    uint8_t cr2 = 0x40;
    swim_write_memory(0x505B, &cr2, 1);
    uint8_t dummy = 0x00;
    swim_write_memory(0x8000, &dummy, 1);
    return swim_wait_flash_ready(5000);
}

//=============================================================================
// SWD协议实现（STM32）
//=============================================================================

#define SWD_ACK_OK      0x1
#define SWD_ACK_WAIT    0x2
#define SWD_ACK_FAULT   0x4

#define DP_IDCODE       0x00
#define DP_ABORT        0x00
#define DP_CTRL_STAT    0x04
#define DP_SELECT       0x08
#define DP_RDBUFF       0x0C

#define AP_CSW          0x00
#define AP_TAR          0x04
#define AP_DRW          0x0C

#define STM32_FLASH_BASE    0x40022000
#define FLASH_KEYR          (STM32_FLASH_BASE + 0x04)
#define FLASH_SR            (STM32_FLASH_BASE + 0x0C)
#define FLASH_CR            (STM32_FLASH_BASE + 0x10)
#define FLASH_AR            (STM32_FLASH_BASE + 0x14)

#define FLASH_KEY1          0x45670123
#define FLASH_KEY2          0xCDEF89AB

static volatile uint32_t g_swd_delay_cycles = 1;

static inline void swd_delay(void) {
    for (volatile uint32_t i = 0; i < g_swd_delay_cycles; i++) {
        __asm volatile("nop");
    }
}

static inline void swd_clock(void) {
    gpio_put(PIN_SWCLK, 0);
    swd_delay();
    gpio_put(PIN_SWCLK, 1);
    swd_delay();
}

static void swd_write_bit(bool bit) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    gpio_put(PIN_SWIM_SWDIO, bit);
    swd_clock();
}

static bool swd_read_bit(void) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_IN);
    swd_clock();
    return gpio_get(PIN_SWIM_SWDIO);
}

static bool calc_parity(uint32_t data) {
    data ^= data >> 16;
    data ^= data >> 8;
    data ^= data >> 4;
    data ^= data >> 2;
    data ^= data >> 1;
    return data & 1;
}

static void swd_line_reset(void) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    gpio_set_dir(PIN_SWCLK, GPIO_OUT);
    
    gpio_put(PIN_SWIM_SWDIO, 1);
    for (int i = 0; i < 60; i++) {
        swd_clock();
    }
    
    uint16_t sync = 0xE79E;
    for (int i = 0; i < 16; i++) {
        swd_write_bit((sync >> i) & 1);
    }
    
    gpio_put(PIN_SWIM_SWDIO, 1);
    for (int i = 0; i < 60; i++) {
        swd_clock();
    }
    
    gpio_put(PIN_SWIM_SWDIO, 0);
    for (int i = 0; i < 8; i++) {
        swd_clock();
    }
}

static bool swd_transfer(bool ap, bool read, uint8_t addr, uint32_t *data) {
    uint8_t request = 0x81;
    request |= (ap ? 0x02 : 0x00);
    request |= (read ? 0x04 : 0x00);
    request |= ((addr & 0x0C) << 1);
    
    bool parity = 0;
    parity ^= ap;
    parity ^= read;
    parity ^= ((addr >> 2) & 1);
    parity ^= ((addr >> 3) & 1);
    request |= (parity ? 0x20 : 0x00);
    
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    for (int i = 0; i < 8; i++) {
        swd_write_bit((request >> i) & 1);
    }
    
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_IN);
    swd_clock();
    
    uint8_t ack = 0;
    for (int i = 0; i < 3; i++) {
        if (swd_read_bit()) {
            ack |= (1 << i);
        }
    }
    
    if (ack != SWD_ACK_OK) {
        gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
        return false;
    }
    
    if (read) {
        uint32_t value = 0;
        for (int i = 0; i < 32; i++) {
            if (swd_read_bit()) {
                value |= (1UL << i);
            }
        }
        bool par = swd_read_bit();
        if (par != calc_parity(value)) {
            gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
            return false;
        }
        *data = value;
        gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    } else {
        gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
        swd_clock();
        uint32_t value = *data;
        for (int i = 0; i < 32; i++) {
            swd_write_bit((value >> i) & 1);
        }
        swd_write_bit(calc_parity(value));
    }
    
    gpio_put(PIN_SWIM_SWDIO, 0);
    for (int i = 0; i < 8; i++) {
        swd_clock();
    }
    
    return true;
}

static bool swd_init(void) {
    gpio_put(PIN_NRST, 0);
    sleep_ms(10);
    gpio_put(PIN_NRST, 1);
    sleep_ms(10);
    
    swd_line_reset();
    
    uint32_t idcode = 0;
    if (!swd_transfer(false, true, DP_IDCODE, &idcode)) {
        return false;
    }
    if (idcode == 0 || idcode == 0xFFFFFFFF) {
        return false;
    }
    
    uint32_t ctrl = 0x50000000;
    if (!swd_transfer(false, false, DP_CTRL_STAT, &ctrl)) {
        return false;
    }
    
    sleep_ms(10);
    
    uint32_t abort = 0x1E;
    swd_transfer(false, false, DP_ABORT, &abort);
    
    return true;
}

static bool swd_read_device_id(uint16_t *device_id) {
    if (!swd_init()) {
        return false;
    }
    
    uint32_t select = 0x00000000;
    if (!swd_transfer(false, false, DP_SELECT, &select)) {
        return false;
    }
    
    uint32_t csw = 0x23000002;
    if (!swd_transfer(true, false, AP_CSW, &csw)) {
        return false;
    }
    
    uint32_t tar = 0xE0042000;
    if (!swd_transfer(true, false, AP_TAR, &tar)) {
        return false;
    }
    
    uint32_t dummy = 0;
    if (!swd_transfer(true, true, AP_DRW, &dummy)) {
        return false;
    }
    
    uint32_t idcode = 0;
    if (!swd_transfer(false, true, DP_RDBUFF, &idcode)) {
        return false;
    }
    
    *device_id = idcode & 0x0FFF;
    if (*device_id == 0x0000 || *device_id == 0x0FFF) {
        return false;
    }
    
    return true;
}

static bool swd_read_memory(uint32_t addr, uint8_t *data, uint32_t len) {
    uint32_t select = 0x00000000;
    if (!swd_transfer(false, false, DP_SELECT, &select)) {
        return false;
    }
    
    uint32_t csw = 0x23000002;
    if (!swd_transfer(true, false, AP_CSW, &csw)) {
        return false;
    }
    
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t tar = addr + i;
        if (!swd_transfer(true, false, AP_TAR, &tar)) {
            return false;
        }
        uint32_t dummy = 0;
        if (!swd_transfer(true, true, AP_DRW, &dummy)) {
            return false;
        }
        uint32_t value = 0;
        if (!swd_transfer(false, true, DP_RDBUFF, &value)) {
            return false;
        }
        uint32_t copy_len = (len - i) < 4 ? (len - i) : 4;
        memcpy(data + i, &value, copy_len);
    }
    
    return true;
}

static bool swd_write_memory(uint32_t addr, const uint8_t *data, uint32_t len) {
    uint32_t select = 0x00000000;
    if (!swd_transfer(false, false, DP_SELECT, &select)) {
        return false;
    }
    
    uint32_t csw = 0x23000002;
    if (!swd_transfer(true, false, AP_CSW, &csw)) {
        return false;
    }
    
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t tar = addr + i;
        if (!swd_transfer(true, false, AP_TAR, &tar)) {
            return false;
        }
        uint32_t value = 0xFFFFFFFF;
        uint32_t copy_len = (len - i) < 4 ? (len - i) : 4;
        memcpy(&value, data + i, copy_len);
        if (!swd_transfer(true, false, AP_DRW, &value)) {
            return false;
        }
    }
    
    return true;
}

static bool stm32_flash_unlock(void) {
    uint32_t key1 = FLASH_KEY1;
    if (!swd_write_memory(FLASH_KEYR, (uint8_t*)&key1, 4)) {
        return false;
    }
    uint32_t key2 = FLASH_KEY2;
    if (!swd_write_memory(FLASH_KEYR, (uint8_t*)&key2, 4)) {
        return false;
    }
    sleep_ms(10);
    return true;
}

static bool stm32_wait_flash_ready(uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - start < timeout_ms) {
        uint32_t sr;
        if (!swd_read_memory(FLASH_SR, (uint8_t*)&sr, 4)) {
            return false;
        }
        if ((sr & 0x01) == 0) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

static bool stm32_mass_erase(void) {
    if (!stm32_flash_unlock()) {
        return false;
    }
    if (!stm32_wait_flash_ready(1000)) {
        return false;
    }
    uint32_t cr = 0x04;
    if (!swd_write_memory(FLASH_CR, (uint8_t*)&cr, 4)) {
        return false;
    }
    cr = 0x44;
    if (!swd_write_memory(FLASH_CR, (uint8_t*)&cr, 4)) {
        return false;
    }
    return stm32_wait_flash_ready(10000);
}

//=============================================================================
// Intel HEX解析
//=============================================================================

typedef struct {
    uint8_t type;
    uint16_t addr;
    uint8_t count;
    uint8_t data[256];
    uint32_t ext_addr;
} hex_record_t;

static bool hex_parse_line(const char *line, hex_record_t *record) {
    if (line[0] != ':') return false;
    
    char buf[3] = {0};
    
    buf[0] = line[1]; buf[1] = line[2];
    record->count = strtoul(buf, NULL, 16);
    
    buf[0] = line[3]; buf[1] = line[4];
    uint8_t addr_h = strtoul(buf, NULL, 16);
    buf[0] = line[5]; buf[1] = line[6];
    uint8_t addr_l = strtoul(buf, NULL, 16);
    record->addr = (addr_h << 8) | addr_l;
    
    buf[0] = line[7]; buf[1] = line[8];
    record->type = strtoul(buf, NULL, 16);
    
    for (int i = 0; i < record->count; i++) {
        buf[0] = line[9 + i * 2];
        buf[1] = line[10 + i * 2];
        record->data[i] = strtoul(buf, NULL, 16);
    }
    
    uint8_t checksum = 0;
    for (int i = 1; i < 9 + record->count * 2; i += 2) {
        buf[0] = line[i];
        buf[1] = line[i + 1];
        checksum += strtoul(buf, NULL, 16);
    }
    buf[0] = line[9 + record->count * 2];
    buf[1] = line[10 + record->count * 2];
    uint8_t file_checksum = strtoul(buf, NULL, 16);
    
    return ((checksum + file_checksum) & 0xFF) == 0;
}

static bool hex_to_bin(const char *hex_text, uint32_t hex_size, 
                      uint8_t **bin_data, uint32_t *bin_size, uint32_t *base_addr) {
    const char *p = hex_text;
    char line[600];
    hex_record_t record = {0};
    uint32_t ext_addr = 0;
    uint32_t min_addr = 0xFFFFFFFF;
    uint32_t max_addr = 0;
    
    uint8_t *temp_buffer = malloc(512 * 1024);
    if (!temp_buffer) return false;
    memset(temp_buffer, 0xFF, 512 * 1024);
    
    while (*p) {
        int i = 0;
        while (*p && *p != '\n' && *p != '\r' && i < sizeof(line) - 1) {
            line[i++] = *p++;
        }
        line[i] = '\0';
        while (*p == '\n' || *p == '\r') p++;
        
        if (line[0] != ':') continue;
        
        if (!hex_parse_line(line, &record)) {
            free(temp_buffer);
            return false;
        }
        
        switch (record.type) {
            case 0x00: {
                uint32_t addr = ext_addr + record.addr;
                if (addr < min_addr) min_addr = addr;
                if (addr + record.count > max_addr) max_addr = addr + record.count;
                if (addr < 512 * 1024) {
                    memcpy(temp_buffer + addr, record.data, record.count);
                }
                break;
            }
            case 0x01:
                goto parse_done;
            case 0x04:
                ext_addr = ((uint32_t)record.data[0] << 24) | 
                          ((uint32_t)record.data[1] << 16);
                break;
            case 0x05:
                break;
            default:
                free(temp_buffer);
                return false;
        }
    }
    
parse_done:
    
    if (max_addr <= min_addr || max_addr - min_addr > 512 * 1024) {
        free(temp_buffer);
        return false;
    }
    
    *base_addr = min_addr;
    *bin_size = max_addr - min_addr;
    *bin_data = malloc(*bin_size);
    
    if (!*bin_data) {
        free(temp_buffer);
        return false;
    }
    
    memcpy(*bin_data, temp_buffer + min_addr, *bin_size);
    free(temp_buffer);
    
    return true;
}

//=============================================================================
// 芯片检测和烧录
//=============================================================================

static bool programmer_detect_chip(chip_type_t *type, uint16_t *device_id) {
    if (swim_read_device_id(device_id)) {
        *type = CHIP_TYPE_STM8;
        return true;
    }
    if (swd_read_device_id(device_id)) {
        *type = CHIP_TYPE_STM32;
        return true;
    }
    return false;
}

static bool programmer_stability_check(chip_type_t *detected_type, uint16_t *device_id) {
    uint16_t ids[STABILITY_CHECK_TIMES];
    chip_type_t types[STABILITY_CHECK_TIMES];
    
    for (int i = 0; i < STABILITY_CHECK_TIMES; i++) {
        if (!programmer_detect_chip(&types[i], &ids[i])) {
            return false;
        }
        sleep_ms(100);
    }
    
    for (int i = 1; i < STABILITY_CHECK_TIMES; i++) {
        if (ids[i] != ids[0] || types[i] != types[0]) {
            return false;
        }
    }
    
    *detected_type = types[0];
    *device_id = ids[0];
    return true;
}

static bool programmer_auto_detect(programmer_state_t *state) {
    chip_type_t type;
    uint16_t device_id;
    
    if (state->config.stability_check_enabled) {
        if (!programmer_stability_check(&type, &device_id)) {
            state->error_code = ERR_CONNECTION_UNSTABLE;
            return false;
        }
    } else {
        if (!programmer_detect_chip(&type, &device_id)) {
            state->error_code = ERR_NO_CHIP_DETECTED;
            return false;
        }
    }
    
    chip_info_t *found = chip_db_find_by_id(device_id, type);
    if (found) {
        memcpy(&state->detected_chip, found, sizeof(chip_info_t));
        state->chip_detected = true;
        return true;
    }
    
    if (state->config.allow_unknown_chip) {
        snprintf(state->detected_chip.name, sizeof(state->detected_chip.name), 
                "未知_%04X", device_id);
        state->detected_chip.device_id = device_id;
        state->detected_chip.type = type;
        state->detected_chip.flash_addr = (type == CHIP_TYPE_STM32) ? 0x08000000 : 0x8000;
        state->detected_chip.flash_size = 64 * 1024;
        state->detected_chip.page_size = 1024;
        state->detected_chip.valid = true;
        state->chip_detected = true;
        return true;
    }
    
    state->error_code = ERR_UNKNOWN_CHIP;
    return false;
}

static bool programmer_erase_chip(programmer_state_t *state) {
    if (state->detected_chip.type == CHIP_TYPE_STM8) {
        return swim_erase_chip();
    } else if (state->detected_chip.type == CHIP_TYPE_STM32) {
        return stm32_mass_erase();
    }
    return false;
}

static bool programmer_write_flash_stream(programmer_state_t *state, 
                                         const uint8_t *data, uint32_t addr,
                                         uint32_t total_size) {
    uint32_t offset = 0;
    uint32_t block_size = (state->detected_chip.type == CHIP_TYPE_STM8) ? 
                         STM8_WRITE_BLOCK : STM32_WRITE_BLOCK;
    
    state->start_time = to_ms_since_boot(get_absolute_time());
    
    // 先解锁
    if (state->detected_chip.type == CHIP_TYPE_STM8) {
        swim_unlock_flash();
    } else {
        stm32_flash_unlock();
    }
    
    while (offset < total_size) {
        uint32_t write_size = (total_size - offset) > block_size ? 
                             block_size : (total_size - offset);
        
        bool success = false;
        if (state->detected_chip.type == CHIP_TYPE_STM8) {
            success = swim_write_memory(addr + offset, data + offset, write_size);
        } else {
            success = swd_write_memory(addr + offset, data + offset, write_size);
        }
        
        if (!success) {
            state->error_code = ERR_WRITE_FAILED;
            return false;
        }
        
        offset += write_size;
        state->prog_progress = offset;
        state->prog_total = total_size;
        
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - state->start_time;
        if (elapsed > 0) {
            state->prog_speed = (offset * 1000) / elapsed;
        }
    }
    
    return true;
}

static bool programmer_read_flash_stream(programmer_state_t *state,
                                        uint8_t *data, uint32_t addr,
                                        uint32_t total_size) {
    uint32_t offset = 0;
    uint32_t block_size = STREAM_BUFFER_SIZE;
    
    state->start_time = to_ms_since_boot(get_absolute_time());
    
    while (offset < total_size) {
        uint32_t read_size = (total_size - offset) > block_size ? 
                            block_size : (total_size - offset);
        
        bool success = false;
        if (state->detected_chip.type == CHIP_TYPE_STM8) {
            success = swim_read_memory(addr + offset, data + offset, read_size);
        } else {
            success = swd_read_memory(addr + offset, data + offset, read_size);
        }
        
        if (!success) {
            state->error_code = ERR_READ_FAILED;
            return false;
        }
        
        offset += read_size;
        state->prog_progress = offset;
        state->prog_total = total_size;
        
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - state->start_time;
        if (elapsed > 0) {
            state->prog_speed = (offset * 1000) / elapsed;
        }
    }
    
    return true;
}

static bool programmer_verify_flash(programmer_state_t *state,
                                   const uint8_t *data, uint32_t addr,
                                   uint32_t total_size) {
    uint32_t offset = 0;
    state->start_time = to_ms_since_boot(get_absolute_time());
    
    while (offset < total_size) {
        uint32_t verify_size = (total_size - offset) > STREAM_BUFFER_SIZE ? 
                              STREAM_BUFFER_SIZE : (total_size - offset);
        
        bool success = false;
        if (state->detected_chip.type == CHIP_TYPE_STM8) {
            success = swim_read_memory(addr + offset, g_stream_buffer, verify_size);
        } else {
            success = swd_read_memory(addr + offset, g_stream_buffer, verify_size);
        }
        
        if (!success) {
            state->error_code = ERR_READ_FAILED;
            return false;
        }
        
        if (memcmp(data + offset, g_stream_buffer, verify_size) != 0) {
            state->error_code = ERR_VERIFY_FAILED;
            return false;
        }
        
        offset += verify_size;
        state->prog_progress = offset;
        state->prog_total = total_size;
        
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - state->start_time;
        if (elapsed > 0) {
            state->prog_speed = (offset * 1000) / elapsed;
        }
    }
    
    return true;
}

//=============================================================================
// 文件系统（简化实现）
//=============================================================================

typedef struct {
    char filename[MAX_FILENAME_LEN];
    uint32_t offset;
    uint32_t size;
    uint32_t timestamp;
    file_type_t type;
    bool valid;
} fs_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t next_offset;
    fs_entry_t entries[MAX_FILES];
} fs_header_t;

#define FS_MAGIC 0x5354464C
#define FS_BLOCK_SIZE 4096

static fs_header_t g_fs_header = {0};

static bool fs_init(void) {
    const uint8_t *flash_fs = (const uint8_t *)(XIP_BASE + FILESYSTEM_OFFSET);
    memcpy(&g_fs_header, flash_fs, sizeof(fs_header_t));
    
    if (g_fs_header.magic != FS_MAGIC) {
        memset(&g_fs_header, 0, sizeof(fs_header_t));
        g_fs_header.magic = FS_MAGIC;
        g_fs_header.version = 1;
        g_fs_header.entry_count = 0;
        g_fs_header.next_offset = sizeof(fs_header_t);
        return true;
    }
    
    return true;
}

static bool fs_save_header(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FILESYSTEM_OFFSET, FS_BLOCK_SIZE);
    flash_range_program(FILESYSTEM_OFFSET, (uint8_t *)&g_fs_header, sizeof(fs_header_t));
    restore_interrupts(ints);
    return true;
}

static bool fs_list_files(file_info_t *files, int *count, int max_count) {
    *count = 0;
    for (int i = 0; i < g_fs_header.entry_count && *count < max_count; i++) {
        if (g_fs_header.entries[i].valid) {
            strcpy(files[*count].filename, g_fs_header.entries[i].filename);
            files[*count].type = g_fs_header.entries[i].type;
            files[*count].size = g_fs_header.entries[i].size;
            files[*count].timestamp = g_fs_header.entries[i].timestamp;
            files[*count].valid = true;
            (*count)++;
        }
    }
    return true;
}

static bool fs_read_file(const char *filename, uint8_t **buffer, uint32_t *size) {
    for (int i = 0; i < g_fs_header.entry_count; i++) {
        if (g_fs_header.entries[i].valid && 
            strcmp(g_fs_header.entries[i].filename, filename) == 0) {
            
            const uint8_t *flash_data = (const uint8_t *)
                (XIP_BASE + FILESYSTEM_OFFSET + g_fs_header.entries[i].offset);
            
            *size = g_fs_header.entries[i].size;
            *buffer = malloc(*size);
            if (!*buffer) return false;
            
            memcpy(*buffer, flash_data, *size);
            return true;
        }
    }
    return false;
}

static bool fs_write_file(const char *filename, const uint8_t *data, 
                         uint32_t size, file_type_t type) {
    if (g_fs_header.entry_count >= MAX_FILES) {
        return false;
    }
    
    if (g_fs_header.next_offset + size > FILESYSTEM_SIZE) {
        return false;
    }
    
    uint32_t write_offset = FILESYSTEM_OFFSET + g_fs_header.next_offset;
    uint32_t aligned_size = (size + 255) & ~255;
    
    uint32_t ints = save_and_disable_interrupts();
    uint32_t blocks = (aligned_size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    flash_range_erase(write_offset, blocks * FS_BLOCK_SIZE);
    flash_range_program(write_offset, data, aligned_size);
    restore_interrupts(ints);
    
    int idx = g_fs_header.entry_count;
    strcpy(g_fs_header.entries[idx].filename, filename);
    g_fs_header.entries[idx].offset = g_fs_header.next_offset;
    g_fs_header.entries[idx].size = size;
    g_fs_header.entries[idx].timestamp = to_ms_since_boot(get_absolute_time());
    g_fs_header.entries[idx].type = type;
    g_fs_header.entries[idx].valid = true;
    
    g_fs_header.next_offset += aligned_size;
    g_fs_header.entry_count++;
    
    return fs_save_header();
}

static bool fs_delete_file(const char *filename) {
    for (int i = 0; i < g_fs_header.entry_count; i++) {
        if (g_fs_header.entries[i].valid && 
            strcmp(g_fs_header.entries[i].filename, filename) == 0) {
            g_fs_header.entries[i].valid = false;
            return fs_save_header();
        }
    }
    return false;
}
//=============================================================================
// 完整烧录流程
//=============================================================================

static bool programmer_full_process(programmer_state_t *state, const char *firmware_file) {
    state->error_code = ERR_OK;
    state->prog_progress = 0;
    state->prog_total = 100;
    
    // 1. 连接芯片
    state->prog_state = PROG_STATE_CONNECTING;
    strcpy(state->prog_message, "连接中...");
    
    // 2. 检测芯片
    state->prog_state = PROG_STATE_DETECTING;
    strcpy(state->prog_message, "检测中...");
    
    if (!programmer_auto_detect(state)) {
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "检测失败");
        return false;
    }
    
    snprintf(state->prog_message, sizeof(state->prog_message), 
            "检测到: %s", state->detected_chip.name);
    sleep_ms(500);
    
    // 3. 加载固件
    state->prog_state = PROG_STATE_LOADING;
    strcpy(state->prog_message, "加载固件...");
    
    uint8_t *file_buffer = NULL;
    uint32_t file_size = 0;
    
    if (!fs_read_file(firmware_file, &file_buffer, &file_size)) {
        state->error_code = ERR_FILE_NOT_FOUND;
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "文件未找到");
        return false;
    }
    
    // 4. 解析固件
    uint8_t *bin_data = NULL;
    uint32_t bin_size = 0;
    uint32_t base_addr = 0;
    
    if (strstr(firmware_file, ".HEX") || strstr(firmware_file, ".hex")) {
        strcpy(state->prog_message, "解析HEX...");
        
        if (!hex_to_bin((char *)file_buffer, file_size, &bin_data, &bin_size, &base_addr)) {
            free(file_buffer);
            state->error_code = ERR_PARSE_HEX;
            state->prog_state = PROG_STATE_ERROR;
            strcpy(state->prog_message, "HEX解析失败");
            return false;
        }
        
        free(file_buffer);
        file_buffer = bin_data;
        file_size = bin_size;
    } else {
        base_addr = state->detected_chip.flash_addr;
    }
    
    // 5. 备份（可选）
    if (state->config.backup_before_program) {
        state->prog_state = PROG_STATE_BACKING_UP;
        strcpy(state->prog_message, "备份中...");
        
        uint8_t *backup_buffer = malloc(state->detected_chip.flash_size);
        if (backup_buffer) {
            if (programmer_read_flash_stream(state, backup_buffer,
                                           state->detected_chip.flash_addr,
                                           state->detected_chip.flash_size)) {
                char backup_name[MAX_FILENAME_LEN];
                snprintf(backup_name, sizeof(backup_name), "BAK_%s_%lu.BIN",
                        state->detected_chip.name,
                        to_ms_since_boot(get_absolute_time()));
                fs_write_file(backup_name, backup_buffer, 
                            state->detected_chip.flash_size, FILE_TYPE_BIN);
            }
            free(backup_buffer);
        }
    }
    
    // 6. 擦除
    if (state->config.auto_erase) {
        state->prog_state = PROG_STATE_ERASING;
        strcpy(state->prog_message, "擦除中...");
        
        if (!programmer_erase_chip(state)) {
            free(file_buffer);
            state->error_code = ERR_ERASE_FAILED;
            state->prog_state = PROG_STATE_ERROR;
            strcpy(state->prog_message, "擦除失败");
            return false;
        }
    }
    
    // 7. 烧录
    state->prog_state = PROG_STATE_WRITING;
    strcpy(state->prog_message, "烧录中...");
    state->prog_progress = 0;
    state->prog_total = file_size;
    
    if (!programmer_write_flash_stream(state, file_buffer, base_addr, file_size)) {
        free(file_buffer);
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "烧录失败");
        return false;
    }
    
    // 8. 校验
    if (state->config.verify_after_program) {
        state->prog_state = PROG_STATE_VERIFYING;
        strcpy(state->prog_message, "校验中...");
        state->prog_progress = 0;
        
        if (!programmer_verify_flash(state, file_buffer, base_addr, file_size)) {
            free(file_buffer);
            state->prog_state = PROG_STATE_ERROR;
            strcpy(state->prog_message, "校验失败");
            return false;
        }
    }
    
    // 9. 完成
    free(file_buffer);
    
    state->prog_state = PROG_STATE_SUCCESS;
    strcpy(state->prog_message, "烧录成功!");
    
    return true;
}

//=============================================================================
// 完整菜单系统
//=============================================================================

static const char *main_menu_items[] = {
    "开始烧录",
    "读取备份",
    "擦除芯片",
    "固件管理",
    "系统设置"
};
#define MAIN_MENU_COUNT 5

static const char *read_menu_items[] = {
    "读取全部",
    "读取范围",
    "备份列表",
    "芯片信息"
};
#define READ_MENU_COUNT 4

static const char *firmware_menu_items[] = {
    "固件列表",
    "删除文件",
    "文件信息"
};
#define FIRMWARE_MENU_COUNT 3

static const char *settings_menu_items[] = {
    "烧录模式",
    "接口模式",
    "稳定检测",
    "允许未知",
    "自动校验",
    "自动备份",
    "自动擦除",
    "SWD速度",
    "屏幕亮度",
    "恢复默认"
};
#define SETTINGS_MENU_COUNT 10

static const char *program_mode_str[] = {"自动", "手动"};
static const char *interface_mode_str[] = {"自动", "SWIM", "SWD"};
static const char *swd_speed_str[] = {"低速", "中速", "高速"};
static const char *bool_str[] = {"关闭", "开启"};

#define MENU_LINE_HEIGHT    11
#define MENU_START_Y        15
#define MENU_ITEMS_PER_PAGE 4

static void menu_draw_header(const char *title) {
    oled_show_string(5, 0, title);
    oled_draw_hline(0, 12, OLED_WIDTH);
}

static void menu_draw_items(const char **items, int count, int selected, int scroll) {
    int y = MENU_START_Y;
    int visible = MENU_ITEMS_PER_PAGE;
    
    for (int i = 0; i < visible && (scroll + i) < count; i++) {
        int index = scroll + i;
        
        if (index == selected) {
            oled_show_string(0, y, ">");
        }
        
        oled_show_string(12, y, items[index]);
        y += MENU_LINE_HEIGHT;
    }
    
    if (count > visible) {
        int bar_height = (OLED_HEIGHT - MENU_START_Y) * visible / count;
        int bar_pos = (OLED_HEIGHT - MENU_START_Y - bar_height) * scroll / (count - visible);
        oled_draw_rect(OLED_WIDTH - 2, MENU_START_Y + bar_pos, 2, bar_height, true);
    }
}

static void menu_draw_main(void) {
    oled_clear();
    menu_draw_header("STM烧录器");
    menu_draw_items(main_menu_items, MAIN_MENU_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    
    if (g_state.chip_connected) {
        oled_show_string(88, 0, "已连接");
    }
    
    oled_refresh();
}

static void menu_draw_read(void) {
    oled_clear();
    menu_draw_header("读取备份");
    menu_draw_items(read_menu_items, READ_MENU_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    oled_refresh();
}

static void menu_draw_firmware(void) {
    oled_clear();
    menu_draw_header("固件管理");
    menu_draw_items(firmware_menu_items, FIRMWARE_MENU_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    oled_refresh();
}

static void menu_draw_settings(void) {
    oled_clear();
    menu_draw_header("系统设置");
    
    int y = MENU_START_Y;
    int visible = MENU_ITEMS_PER_PAGE;
    
    for (int i = 0; i < visible && (g_menu.scroll_offset + i) < SETTINGS_MENU_COUNT; i++) {
        int index = g_menu.scroll_offset + i;
        
        if (index == g_menu.selected_item) {
            oled_show_string(0, y, ">");
        }
        
        oled_show_string(12, y, settings_menu_items[index]);
        
        // 显示当前值
        const char *value_str = "";
        switch (index) {
            case 0: value_str = program_mode_str[g_state.config.program_mode]; break;
            case 1: value_str = interface_mode_str[g_state.config.interface_mode]; break;
            case 2: value_str = bool_str[g_state.config.stability_check_enabled]; break;
            case 3: value_str = bool_str[g_state.config.allow_unknown_chip]; break;
            case 4: value_str = bool_str[g_state.config.verify_after_program]; break;
            case 5: value_str = bool_str[g_state.config.backup_before_program]; break;
            case 6: value_str = bool_str[g_state.config.auto_erase]; break;
            case 7: value_str = swd_speed_str[g_state.config.swd_speed]; break;
            case 8: {
                static char bright_str[8];
                snprintf(bright_str, sizeof(bright_str), "%d", g_state.config.brightness);
                value_str = bright_str;
                break;
            }
        }
        
        oled_show_string(OLED_WIDTH - 36, y, value_str);
        y += MENU_LINE_HEIGHT;
    }
    
    if (SETTINGS_MENU_COUNT > visible) {
        int bar_height = (OLED_HEIGHT - MENU_START_Y) * visible / SETTINGS_MENU_COUNT;
        int bar_pos = (OLED_HEIGHT - MENU_START_Y - bar_height) * g_menu.scroll_offset / 
                     (SETTINGS_MENU_COUNT - visible);
        oled_draw_rect(OLED_WIDTH - 2, MENU_START_Y + bar_pos, 2, bar_height, true);
    }
    
    oled_refresh();
}

static void menu_draw_chip_select(void) {
    oled_clear();
    menu_draw_header("选择芯片");
    
    int y = MENU_START_Y;
    int visible = MENU_ITEMS_PER_PAGE;
    
    for (int i = 0; i < visible && (g_menu.scroll_offset + i) < g_chip_db_count; i++) {
        int index = g_menu.scroll_offset + i;
        
        if (g_chip_db[index].valid) {
            if (index == g_menu.selected_item) {
                oled_show_string(0, y, ">");
            }
            
            oled_show_string(12, y, g_chip_db[index].name);
            y += MENU_LINE_HEIGHT;
        }
    }
    
    if (g_chip_db_count > visible) {
        int bar_height = (OLED_HEIGHT - MENU_START_Y) * visible / g_chip_db_count;
        int bar_pos = (OLED_HEIGHT - MENU_START_Y - bar_height) * g_menu.scroll_offset / 
                     (g_chip_db_count - visible);
        oled_draw_rect(OLED_WIDTH - 2, MENU_START_Y + bar_pos, 2, bar_height, true);
    }
    
    oled_refresh();
}

static void menu_draw_firmware_select(void) {
    oled_clear();
    menu_draw_header("选择固件");
    
    int y = MENU_START_Y;
    int visible = MENU_ITEMS_PER_PAGE;
    
    for (int i = 0; i < visible && (g_menu.scroll_offset + i) < g_state.file_count; i++) {
        int index = g_menu.scroll_offset + i;
        
        if (index == g_menu.selected_item) {
            oled_show_string(0, y, ">");
        }
        
        char short_name[20];
        strncpy(short_name, g_state.file_list[index].filename, 18);
        short_name[18] = 0;
        if (strlen(g_state.file_list[index].filename) > 18) {
            strcat(short_name, "..");
        }
        
        oled_show_string(12, y, short_name);
        y += MENU_LINE_HEIGHT;
    }
    
    if (g_state.file_count > visible) {
        int bar_height = (OLED_HEIGHT - MENU_START_Y) * visible / g_state.file_count;
        int bar_pos = (OLED_HEIGHT - MENU_START_Y - bar_height) * g_menu.scroll_offset / 
                     (g_state.file_count - visible);
        oled_draw_rect(OLED_WIDTH - 2, MENU_START_Y + bar_pos, 2, bar_height, true);
    }
    
    oled_refresh();
}

static void menu_draw_progress(void) {
    oled_clear();
    
    // 芯片名称
    oled_show_string(0, 0, g_state.detected_chip.name);
    
    // 状态信息
    oled_show_string(0, 13, g_state.prog_message);
    
    // 进度条
    if (g_state.prog_total > 0) {
        oled_show_progress(5, 27, OLED_WIDTH - 10, 10,
                          g_state.prog_progress, g_state.prog_total);
        
        // 百分比
        oled_show_percent(OLED_WIDTH / 2 - 12, 40,
                         g_state.prog_progress, g_state.prog_total);
        
        // 速度
        oled_show_speed(5, 52, g_state.prog_speed);
        
        // 字节数
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu/%lu",
                (unsigned long)g_state.prog_progress,
                (unsigned long)g_state.prog_total);
        oled_show_string(OLED_WIDTH - 60, 52, buf);
    }
    
    // 错误码
    if (g_state.error_code != 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "错误:%d", g_state.error_code);
        oled_show_string(OLED_WIDTH - 42, 0, buf);
    }
    
    oled_refresh();
}

static void menu_draw_confirm(const char *message) {
    oled_clear();
    
    oled_show_string(10, 15, message);
    
    oled_show_string(20, 40, "确认");
    oled_show_string(70, 40, "取消");
    
    if (g_menu.selected_item == 0) {
        oled_show_string(10, 40, ">");
    } else {
        oled_show_string(60, 40, ">");
    }
    
    oled_refresh();
}

static void menu_draw_info(void) {
    oled_clear();
    
    oled_show_string(5, 0, g_menu.info_title);
    oled_draw_hline(0, 12, OLED_WIDTH);
    
    int y = 15;
    char *line = g_menu.info_message;
    while (*line && y < OLED_HEIGHT - 8) {
        char buf[22];
        int i = 0;
        
        while (*line && *line != '\n' && i < 21) {
            buf[i++] = *line++;
        }
        buf[i] = 0;
        
        if (*line == '\n') line++;
        
        oled_show_string(5, y, buf);
        y += 10;
    }
    
    oled_show_string(OLED_WIDTH / 2 - 12, OLED_HEIGHT - 10, "确认");
    
    oled_refresh();
}

static void menu_navigate(int item_count, key_event_t key) {
    if (key == KEY_UP) {
        if (g_menu.selected_item > 0) {
            g_menu.selected_item--;
            if (g_menu.selected_item < g_menu.scroll_offset) {
                g_menu.scroll_offset = g_menu.selected_item;
            }
            g_menu.need_refresh = true;
        }
    } else if (key == KEY_DOWN) {
        if (g_menu.selected_item < item_count - 1) {
            g_menu.selected_item++;
            if (g_menu.selected_item >= g_menu.scroll_offset + MENU_ITEMS_PER_PAGE) {
                g_menu.scroll_offset = g_menu.selected_item - MENU_ITEMS_PER_PAGE + 1;
            }
            g_menu.need_refresh = true;
        }
    }
}

void menu_system_init(void) {
    g_menu.current_menu = MENU_MAIN;
    g_menu.selected_item = 0;
    g_menu.scroll_offset = 0;
    g_menu.need_refresh = true;
}

void menu_system_process(menu_state_t *menu, key_event_t key, programmer_state_t *state) {
    if (key == KEY_NONE) return;
    
    switch (menu->current_menu) {
        case MENU_MAIN:
            menu_navigate(MAIN_MENU_COUNT, key);
            
            if (key == KEY_OK) {
                menu->previous_menu = MENU_MAIN;
                switch (menu->selected_item) {
                    case 0: // 开始烧录
                        if (state->file_count > 0) {
                            menu->current_menu = MENU_FIRMWARE_SELECT;
                            menu->selected_item = 0;
                            menu->scroll_offset = 0;
                        } else {
                            strcpy(menu->info_title, "提示");
                            strcpy(menu->info_message, "没有可用固件\n请先添加固件文件");
                            menu->current_menu = MENU_INFO;
                        }
                        break;
                    case 1: // 读取备份
                        menu->current_menu = MENU_READ_BACKUP;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        break;
                    case 2: // 擦除芯片
                        menu->current_menu = MENU_CHIP_ERASE;
                        menu->selected_item = 0;
                        break;
                    case 3: // 固件管理
                        menu->current_menu = MENU_FIRMWARE_MGMT;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        break;
                    case 4: // 系统设置
                        menu->current_menu = MENU_SETTINGS;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        break;
                }
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_main();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_READ_BACKUP:
            menu_navigate(READ_MENU_COUNT, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->scroll_offset = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                switch (menu->selected_item) {
                    case 0: // 读取全部
                        menu->current_menu = MENU_PROGRAM_PROGRESS;
                        state->prog_state = PROG_STATE_IDLE;
                        menu->user_data = (void *)1; // 标记为读取模式
                        menu->need_refresh = true;
                        break;
                    case 2: // 备份列表
                        strcpy(menu->info_title, "备份列表");
                        snprintf(menu->info_message, sizeof(menu->info_message),
                                "共有 %d 个备份文件", state->file_count);
                        menu->current_menu = MENU_INFO;
                        menu->need_refresh = true;
                        break;
                    case 3: // 芯片信息
                        if (programmer_auto_detect(state)) {
                            strcpy(menu->info_title, "芯片信息");
                            snprintf(menu->info_message, sizeof(menu->info_message),
                                    "%s\nFlash:%luK\nRAM:%luK",
                                    state->detected_chip.name,
                                    (unsigned long)(state->detected_chip.flash_size / 1024),
                                    (unsigned long)(state->detected_chip.ram_size / 1024));
                        } else {
                            strcpy(menu->info_title, "错误");
                            strcpy(menu->info_message, "未检测到芯片");
                        }
                        menu->current_menu = MENU_INFO;
                        menu->need_refresh = true;
                        break;
                }
            }
            
            if (menu->need_refresh) {
                menu_draw_read();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_CHIP_ERASE:
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                menu->current_menu = MENU_CONFIRM;
                menu->selected_item = 0;
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                oled_clear();
                menu_draw_header("擦除芯片");
                oled_show_string(20, 30, "按确认键执行");
                oled_refresh();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_FIRMWARE_MGMT:
            menu_navigate(FIRMWARE_MENU_COUNT, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                switch (menu->selected_item) {
                    case 0: // 固件列表
                        if (state->file_count > 0) {
                            menu->current_menu = MENU_FIRMWARE_SELECT;
                            menu->selected_item = 0;
                            menu->scroll_offset = 0;
                        } else {
                            strcpy(menu->info_title, "提示");
                            strcpy(menu->info_message, "没有固件文件");
                            menu->current_menu = MENU_INFO;
                        }
                        break;
                    case 2: // 文件信息
                        strcpy(menu->info_title, "固件文件");
                        snprintf(menu->info_message, sizeof(menu->info_message),
                                "共有 %d 个文件", state->file_count);
                        menu->current_menu = MENU_INFO;
                        break;
                }
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_firmware();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_SETTINGS:
            menu_navigate(SETTINGS_MENU_COUNT, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->scroll_offset = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                switch (menu->selected_item) {
                    case 0: // 烧录模式
                        state->config.program_mode = (state->config.program_mode + 1) % 2;
                        config_save(&state->config);
                        break;
                    case 1: // 接口模式
                        state->config.interface_mode = (state->config.interface_mode + 1) % 3;
                        config_save(&state->config);
                        break;
                    case 2: // 稳定检测
                        state->config.stability_check_enabled = !state->config.stability_check_enabled;
                        config_save(&state->config);
                        break;
                    case 3: // 允许未知
                        state->config.allow_unknown_chip = !state->config.allow_unknown_chip;
                        config_save(&state->config);
                        break;
                    case 4: // 自动校验
                        state->config.verify_after_program = !state->config.verify_after_program;
                        config_save(&state->config);
                        break;
                    case 5: // 自动备份
                        state->config.backup_before_program = !state->config.backup_before_program;
                        config_save(&state->config);
                        break;
                    case 6: // 自动擦除
                        state->config.auto_erase = !state->config.auto_erase;
                        config_save(&state->config);
                        break;
                    case 7: // SWD速度
                        state->config.swd_speed = (state->config.swd_speed + 1) % 3;
                        config_save(&state->config);
                        break;
                    case 8: // 屏幕亮度
                        state->config.brightness += 50;
                        if (state->config.brightness > 250) state->config.brightness = 50;
                        config_save(&state->config);
                        break;
                    case 9: // 恢复默认
                        config_set_default(&state->config);
                        config_save(&state->config);
                        strcpy(menu->info_title, "提示");
                        strcpy(menu->info_message, "已恢复默认设置");
                        menu->current_menu = MENU_INFO;
                        menu->need_refresh = true;
                        return;
                }
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_settings();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_CHIP_SELECT:
            menu_navigate(g_chip_db_count, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = menu->previous_menu;
                menu->selected_item = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                memcpy(&state->selected_chip, &g_chip_db[menu->selected_item], sizeof(chip_info_t));
                menu->current_menu = menu->previous_menu;
                menu->selected_item = 0;
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_chip_select();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_FIRMWARE_SELECT:
            menu_navigate(state->file_count, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = menu->previous_menu;
                menu->selected_item = 0;
                menu->scroll_offset = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                if (menu->previous_menu == MENU_MAIN) {
                    // 开始烧录
                    menu->current_menu = MENU_PROGRAM_PROGRESS;
                    state->prog_state = PROG_STATE_IDLE;
                    menu->user_data = (void *)0; // 标记为烧录模式
                } else {
                    // 查看文件信息
                    strcpy(menu->info_title, "文件信息");
                    snprintf(menu->info_message, sizeof(menu->info_message),
                            "%s\n大小:%lu字节",
                            state->file_list[menu->selected_item].filename,
                            (unsigned long)state->file_list[menu->selected_item].size);
                    menu->current_menu = MENU_INFO;
                }
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_firmware_select();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_PROGRAM_PROGRESS:
            if (state->prog_state == PROG_STATE_SUCCESS ||
                state->prog_state == PROG_STATE_ERROR) {
                if (key == KEY_OK || key == KEY_BACK) {
                    menu->current_menu = MENU_MAIN;
                    menu->selected_item = 0;
                    state->prog_state = PROG_STATE_IDLE;
                    menu->need_refresh = true;
                }
            }
            menu_draw_progress();
            break;
            
        case MENU_CONFIRM:
            if (key == KEY_UP || key == KEY_DOWN) {
                menu->selected_item = !menu->selected_item;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                if (menu->selected_item == 0) {
                    // 确认擦除
                    menu->current_menu = MENU_PROGRAM_PROGRESS;
                    state->prog_state = PROG_STATE_IDLE;
                    menu->user_data = (void *)2; // 标记为擦除模式
                    menu->need_refresh = true;
                } else {
                    // 取消
                    menu->current_menu = MENU_CHIP_ERASE;
                    menu->selected_item = 0;
                    menu->need_refresh = true;
                }
            } else if (key == KEY_BACK) {
                menu->current_menu = MENU_CHIP_ERASE;
                menu->selected_item = 0;
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_confirm("确认擦除芯片?");
                menu->need_refresh = false;
            }
            break;
            
        case MENU_INFO:
            if (key == KEY_OK || key == KEY_BACK) {
                menu->current_menu = menu->previous_menu;
                menu->selected_item = 0;
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_info();
                menu->need_refresh = false;
            }
            break;
            
        default:
            break;
    }
}

void menu_system_draw(menu_state_t *menu, programmer_state_t *state) {
    // 由 menu_system_process 内部调用具体绘制函数
}

//=============================================================================
// 硬件初始化
//=============================================================================

static void hardware_init(void) {
    stdio_init_all();
    
    // I2C初始化
    i2c_init(i2c0, 400000);
    gpio_set_function(PIN_OLED_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_OLED_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_OLED_SDA);
    gpio_pull_up(PIN_OLED_SCL);
    
    // 按键初始化
    gpio_init(PIN_KEY1);
    gpio_init(PIN_KEY2);
    gpio_init(PIN_KEY3);
    gpio_init(PIN_KEY4);
    gpio_set_dir(PIN_KEY1, GPIO_IN);
    gpio_set_dir(PIN_KEY2, GPIO_IN);
    gpio_set_dir(PIN_KEY3, GPIO_IN);
    gpio_set_dir(PIN_KEY4, GPIO_IN);
    gpio_pull_up(PIN_KEY1);
    gpio_pull_up(PIN_KEY2);
    gpio_pull_up(PIN_KEY3);
    gpio_pull_up(PIN_KEY4);
    
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

//=============================================================================
// 系统初始化
//=============================================================================

static bool system_init(void) {
    if (!oled_init()) {
        return false;
    }
    
    oled_clear();
    oled_show_string(30, 20, "STM烧录器");
    oled_show_string(30, 35, "初始化中...");
    oled_refresh();
    
    crc32_init();
    keys_init();
    chip_db_init();
    
    if (!config_load(&g_state.config)) {
        config_set_default(&g_state.config);
        config_save(&g_state.config);
    }
    
    if (!fs_init()) {
        oled_clear();
        oled_show_string(10, 28, "文件系统初始化失败");
        oled_refresh();
        sleep_ms(2000);
        return false;
    }
    
    fs_list_files(g_state.file_list, &g_state.file_count, MAX_FILES);
    
    menu_system_init();
    
    sleep_ms(1000);
    return true;
}

//=============================================================================
// 主循环
//=============================================================================

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

//=============================================================================
// 主函数
//=============================================================================

int main(void) {
    hardware_init();
    
    if (!system_init()) {
        while (1) {
            sleep_ms(1000);
        }
    }
    
    main_loop();
    
    return 0;
}
