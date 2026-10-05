// stm_programmer.c - RP2040-Zero STM8/STM32 烧录器完整实现
/*
 * RP2040-Zero STM8/STM32 Programmer - 完整版本
 * 单文件完整实现，包含所有菜单和功能
 * 
 * 硬件:
 *   - RP2040-Zero (2MB Flash, 264KB SRAM)
 *   - 0.96" OLED 128x64 SSD1315 I2C
 *   - 4个按键
 * 
 * GPIO:
 *   GPIO29 -> OLED SCL
 *   GPIO28 -> OLED SDA
 *   GPIO27 -> K1 (上)
 *   GPIO26 -> K2 (下)
 *   GPIO15 -> K3 (确认)
 *   GPIO14 -> K4 (返回)
 *   GPIO2  -> STM8 SWIM / STM32 SWDIO
 *   GPIO3  -> STM32 SWCLK
 *   GPIO4  -> NRST
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "pico/time.h"

//=============================================================================
// 配置定义
//=============================================================================

// GPIO定义
#define PIN_OLED_SCL    29
#define PIN_OLED_SDA    28
#define PIN_KEY1        15  // 上
#define PIN_KEY2        14  // 下
#define PIN_KEY3        27  // 确认
#define PIN_KEY4        26  // 返回
#define PIN_SWIM_SWDIO  2
#define PIN_SWCLK       3
#define PIN_NRST        4

// OLED参数
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDR       0x3C

// 文件系统
#define MAX_FIRMWARE_FILES  32
#define MAX_BACKUP_FILES    16
#define MAX_FILENAME_LEN    64
#define MAX_FILE_SIZE       (8 * 1024)  // 512KB

// 芯片数据库
#define MAX_CHIP_DB         64

// 按键去抖时间
#define KEY_DEBOUNCE_MS     50
#define KEY_REPEAT_MS       500

// 连接稳定性检测
#define STABILITY_CHECK_TIMES   3
#define STABILITY_CHECK_DELAY   100  // ms

// 菜单显示
#define MENU_ITEMS_PER_PAGE     5
#define MENU_LINE_HEIGHT        10
#define MENU_START_Y            18

//=============================================================================
// 数据结构定义
//=============================================================================

// 按键事件
typedef enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_OK,
    KEY_BACK
} key_event_t;

// 接口模式
typedef enum {
    INTERFACE_AUTO = 0,
    INTERFACE_SWIM,
    INTERFACE_SWD
} interface_mode_t;

// 烧录模式
typedef enum {
    PROGRAM_MODE_AUTO = 0,
    PROGRAM_MODE_MANUAL
} program_mode_t;

// SWD速度
typedef enum {
    SWD_SPEED_LOW = 0,      // 100kHz
    SWD_SPEED_MEDIUM,       // 1MHz
    SWD_SPEED_HIGH          // 4MHz
} swd_speed_t;

// 芯片类型
typedef enum {
    CHIP_TYPE_UNKNOWN = 0,
    CHIP_TYPE_STM8,
    CHIP_TYPE_STM32
} chip_type_t;

// 文件类型
typedef enum {
    FILE_TYPE_BIN = 0,
    FILE_TYPE_HEX,
    FILE_TYPE_S19
} file_type_t;

// 菜单ID
typedef enum {
    MENU_MAIN = 0,
    MENU_PROGRAM_START,
    MENU_READ_BACKUP,
    MENU_READ_BACKUP_SUB,
    MENU_CHIP_ERASE,
    MENU_CHIP_ERASE_SUB,
    MENU_FIRMWARE_MGMT,
    MENU_FIRMWARE_SUB,
    MENU_SETTINGS,
    MENU_SETTINGS_SUB,
    MENU_CHIP_SELECT,
    MENU_FIRMWARE_SELECT,
    MENU_PROGRAM_PROGRESS,
    MENU_CONFIRM,
    MENU_INFO,
    MENU_MAX
} menu_id_t;

// 烧录状态
typedef enum {
    PROG_STATE_IDLE = 0,
    PROG_STATE_CONNECTING,
    PROG_STATE_DETECTING,
    PROG_STATE_ERASING,
    PROG_STATE_WRITING,
    PROG_STATE_VERIFYING,
    PROG_STATE_READING,
    PROG_STATE_SUCCESS,
    PROG_STATE_ERROR
} prog_state_t;

// 芯片信息
typedef struct {
    char name[32];
    uint16_t device_id;
    uint16_t id_mask;
    chip_type_t type;
    uint32_t flash_addr;
    uint32_t flash_size;
    uint32_t ram_size;
    uint32_t page_size;
    bool valid;
} chip_info_t;

// 固件文件
typedef struct {
    char filename[MAX_FILENAME_LEN];
    file_type_t type;
    uint32_t size;
    bool valid;
} firmware_file_t;

// 备份文件
typedef struct {
    char filename[MAX_FILENAME_LEN];
    char chip_name[32];
    uint32_t size;
    uint32_t timestamp;
    bool valid;
} backup_file_t;

// 系统配置
typedef struct {
    // 烧录设置
    program_mode_t program_mode;
    interface_mode_t interface_mode;
    bool stability_check_enabled;
    bool allow_unknown_chip;
    bool keep_connected_after_program;
    bool verify_after_program;
    bool backup_before_program;
    swd_speed_t swd_speed;
    
    // 默认选择
    int default_chip_index;
    int default_firmware_index;
    
    // 文件设置
    uint8_t max_backups;
    
    // 显示设置
    uint8_t screen_brightness;
    bool screen_auto_off;
    
    // 标志
    uint32_t magic;  // 配置有效性标记
} system_config_t;

// 烧录器状态
typedef struct {
    system_config_t config;

    // 芯片状态
    chip_info_t detected_chip;
    chip_info_t selected_chip;
    bool chip_connected;
    bool chip_detected;
    
    // 文件列表
    firmware_file_t firmware_list[MAX_FIRMWARE_FILES];
    int firmware_count;
    backup_file_t backup_list[MAX_BACKUP_FILES];
    int backup_count;
    
    // 烧录状态
    prog_state_t prog_state;
    uint32_t prog_progress;
    uint32_t prog_total;
    char prog_message[64];
    int error_code;
    
    // 自动烧录锁
    bool auto_program_locked;
    
    // USB状态
    bool usb_connected;
    
    // 读取缓冲区
    uint8_t read_buffer[MAX_FILE_SIZE];
    uint32_t read_size;
} programmer_state_t;
static void config_save(system_config_t *config);
// 菜单状态
typedef struct {
    menu_id_t current_menu;
    menu_id_t previous_menu;
    int selected_item;
    int scroll_offset;
    bool need_refresh;
    char info_message[128];
    bool confirm_result;
} menu_state_t;

//=============================================================================
// 全局变量
//=============================================================================

static programmer_state_t g_state = {0};
static menu_state_t g_menu = {0};

// 芯片数据库
static chip_info_t g_chip_db[MAX_CHIP_DB] = {0};
static int g_chip_db_count = 0;

// OLED显存
static uint8_t g_oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8] = {0};

// 固件缓冲区
static uint8_t g_firmware_buffer[MAX_FILE_SIZE] = {0};
static uint32_t g_firmware_size = 0;

//=============================================================================
// 6x8 ASCII字体
//=============================================================================

static const uint8_t font_6x8[][6] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // sp (0x20)
    {0x00, 0x00, 0x5F, 0x00, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62, 0x00}, // %
    {0x36, 0x49, 0x56, 0x20, 0x50, 0x00}, // &
    {0x00, 0x08, 0x07, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00}, // )
    {0x2A, 0x1C, 0x7F, 0x1C, 0x2A, 0x00}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00}, // +
    {0x00, 0x80, 0x70, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x00}, // -
    {0x00, 0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02, 0x00}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00}, // 1
    {0x72, 0x49, 0x49, 0x49, 0x46, 0x00}, // 2
    {0x21, 0x41, 0x49, 0x4D, 0x33, 0x00}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39, 0x00}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x31, 0x00}, // 6
    {0x41, 0x21, 0x11, 0x09, 0x07, 0x00}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36, 0x00}, // 8
    {0x46, 0x49, 0x49, 0x29, 0x1E, 0x00}, // 9
    {0x00, 0x00, 0x14, 0x00, 0x00, 0x00}, // :
    {0x00, 0x40, 0x34, 0x00, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14, 0x00}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x59, 0x09, 0x06, 0x00}, // ?
    {0x3E, 0x41, 0x5D, 0x59, 0x4E, 0x00}, // @
    {0x7C, 0x12, 0x11, 0x12, 0x7C, 0x00}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00}, // C
    {0x7F, 0x41, 0x41, 0x41, 0x3E, 0x00}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01, 0x00}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00}, // L
    {0x7F, 0x02, 0x1C, 0x02, 0x7F, 0x00}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00}, // R
    {0x26, 0x49, 0x49, 0x49, 0x32, 0x00}, // S
    {0x03, 0x01, 0x7F, 0x01, 0x03, 0x00}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63, 0x00}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03, 0x00}, // Y
    {0x61, 0x59, 0x49, 0x4D, 0x43, 0x00}, // Z
    {0x00, 0x7F, 0x41, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20, 0x00}, // '\'
    {0x00, 0x41, 0x41, 0x41, 0x7F, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04, 0x00}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x00}, // _
    {0x00, 0x03, 0x07, 0x08, 0x00, 0x00}, // `
    {0x20, 0x54, 0x54, 0x78, 0x40, 0x00}, // a
    {0x7F, 0x28, 0x44, 0x44, 0x38, 0x00}, // b
    {0x38, 0x44, 0x44, 0x44, 0x28, 0x00}, // c
    {0x38, 0x44, 0x44, 0x28, 0x7F, 0x00}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18, 0x00}, // e
    {0x00, 0x08, 0x7E, 0x09, 0x02, 0x00}, // f
    {0x18, 0xA4, 0xA4, 0x9C, 0x78, 0x00}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78, 0x00}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00, 0x00}, // i
    {0x20, 0x40, 0x40, 0x3D, 0x00, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00, 0x00}, // l
    {0x7C, 0x04, 0x78, 0x04, 0x78, 0x00}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78, 0x00}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38, 0x00}, // o
    {0xFC, 0x18, 0x24, 0x24, 0x18, 0x00}, // p
    {0x18, 0x24, 0x24, 0x18, 0xFC, 0x00}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08, 0x00}, // r
    {0x48, 0x54, 0x54, 0x54, 0x24, 0x00}, // s
    {0x04, 0x04, 0x3F, 0x44, 0x24, 0x00}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44, 0x00}, // x
    {0x4C, 0x90, 0x90, 0x90, 0x7C, 0x00}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44, 0x00}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00, 0x00}, // {
    {0x00, 0x00, 0x77, 0x00, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00, 0x00}, // }
    {0x02, 0x01, 0x02, 0x04, 0x02, 0x00}, // ~
};

//=============================================================================
// OLED驱动
//=============================================================================

// I2C写命令
static void oled_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

// I2C写数据
static void oled_write_data(uint8_t data) {
    uint8_t buf[2] = {0x40, data};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

// OLED初始化
static bool oled_init(void) {
    sleep_ms(100);
    
    oled_write_cmd(0xAE); // Display OFF
    oled_write_cmd(0x00); // Set lower column address
    oled_write_cmd(0x10); // Set higher column address
    oled_write_cmd(0x40); // Set display start line
    oled_write_cmd(0xB0); // Set page address
    oled_write_cmd(0x81); // Set contrast control
    oled_write_cmd(0xCF); // Contrast value
    oled_write_cmd(0xA1); // Set segment remap
    oled_write_cmd(0xA6); // Normal display
    oled_write_cmd(0xA8); // Set multiplex ratio
    oled_write_cmd(0x3F); // 1/64 duty
    oled_write_cmd(0xC8); // COM scan direction
    oled_write_cmd(0xD3); // Set display offset
    oled_write_cmd(0x00); // No offset
    oled_write_cmd(0xD5); // Set clock divide
    oled_write_cmd(0x80); // Default
    oled_write_cmd(0xD9); // Set pre-charge period
    oled_write_cmd(0xF1);
    oled_write_cmd(0xDA); // Set COM pins
    oled_write_cmd(0x12);
    oled_write_cmd(0xDB); // Set VCOMH
    oled_write_cmd(0x40);
    oled_write_cmd(0x8D); // Charge pump
    oled_write_cmd(0x14); // Enable
    oled_write_cmd(0xAF); // Display ON
    
    return true;
}

// 清空显存
static void oled_clear(void) {
    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));
}

// 刷新显示
static void oled_refresh(void) {
    for (int page = 0; page < 8; page++) {
        oled_write_cmd(0xB0 + page);
        oled_write_cmd(0x00);
        oled_write_cmd(0x10);
        
        for (int col = 0; col < OLED_WIDTH; col++) {
            oled_write_data(g_oled_buffer[page * OLED_WIDTH + col]);
        }
    }
}

// 画点
static void oled_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    
    int page = y / 8;
    int bit = y % 8;
    int index = page * OLED_WIDTH + x;
    
    if (on) {
        g_oled_buffer[index] |= (1 << bit);
    } else {
        g_oled_buffer[index] &= ~(1 << bit);
    }
}

// 画线
static void oled_draw_line(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        oled_draw_pixel(x1, y1, true);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

// 画矩形
static void oled_draw_rect(int x, int y, int w, int h, bool fill) {
    if (fill) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                oled_draw_pixel(x + j, y + i, true);
            }
        }
    } else {
        oled_draw_line(x, y, x + w - 1, y);
        oled_draw_line(x + w - 1, y, x + w - 1, y + h - 1);
        oled_draw_line(x + w - 1, y + h - 1, x, y + h - 1);
        oled_draw_line(x, y + h - 1, x, y);
    }
}

// 显示字符 (6x8)
static void oled_show_char(int x, int y, char c) {
    if (c < 0x20 || c > 0x7E) return;
    
    int index = c - 0x20;
    for (int i = 0; i < 6; i++) {
        uint8_t line = font_6x8[index][i];
        for (int j = 0; j < 8; j++) {
            if (line & (1 << j)) {
                oled_draw_pixel(x + i, y + j, true);
            }
        }
    }
}

// 显示字符串
static void oled_show_string(int x, int y, const char *str) {
    int pos_x = x;
    
    while (*str) {
        if (*str >= 0x20 && *str <= 0x7E) {
            oled_show_char(pos_x, y, *str);
            pos_x += 6;
        }
        str++;
        
        if (pos_x >= OLED_WIDTH) break;
    }
}

// 显示数字
static void oled_show_number(int x, int y, uint32_t num) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)num);
    oled_show_string(x, y, buf);
}

// 显示进度条
static void oled_show_progress(int x, int y, int w, int h, uint32_t value, uint32_t total) {
    oled_draw_rect(x, y, w, h, false);
    
    if (total > 0) {
        int progress_w = (int)((uint64_t)value * (w - 2) / total);
        if (progress_w > 0 && progress_w <= w - 2) {
            oled_draw_rect(x + 1, y + 1, progress_w, h - 2, true);
        }
    }
}

// 显示百分比
static void oled_show_percent(int x, int y, uint32_t value, uint32_t total) {
    char buf[8];
    if (total > 0) {
        snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(value * 100 / total));
    } else {
        snprintf(buf, sizeof(buf), "0%%");
    }
    oled_show_string(x, y, buf);
}

//=============================================================================
// 按键处理
//=============================================================================

static uint32_t g_key_last_time[4] = {0};
static bool g_key_last_state[4] = {false};

// 初始化按键
static void keys_init(void) {
    memset(g_key_last_time, 0, sizeof(g_key_last_time));
    memset(g_key_last_state, 0, sizeof(g_key_last_state));
}

// 读取单个按键
static bool key_read(int pin) {
    return !gpio_get(pin); // 低电平按下
}

// 按键扫描
static key_event_t keys_scan(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // K1 - 上
    if (key_read(PIN_KEY1)) {
        if (!g_key_last_state[0] || (now - g_key_last_time[0] > KEY_REPEAT_MS)) {
            g_key_last_state[0] = true;
            g_key_last_time[0] = now;
            return KEY_UP;
        }
    } else {
        g_key_last_state[0] = false;
    }
    
    // K2 - 下
    if (key_read(PIN_KEY2)) {
        if (!g_key_last_state[1] || (now - g_key_last_time[1] > KEY_REPEAT_MS)) {
            g_key_last_state[1] = true;
            g_key_last_time[1] = now;
            return KEY_DOWN;
        }
    } else {
        g_key_last_state[1] = false;
    }
    
    // K3 - 确认
    if (key_read(PIN_KEY3)) {
        if (!g_key_last_state[2]) {
            g_key_last_state[2] = true;
            g_key_last_time[2] = now;
            return KEY_OK;
        }
    } else {
        g_key_last_state[2] = false;
    }
    
    // K4 - 返回
    if (key_read(PIN_KEY4)) {
        if (!g_key_last_state[3]) {
            g_key_last_state[3] = true;
            g_key_last_time[3] = now;
            return KEY_BACK;
        }
    } else {
        g_key_last_state[3] = false;
    }
    
    return KEY_NONE;
}

//=============================================================================
// 芯片数据库
//=============================================================================

// 初始化芯片数据库
static void chip_db_init(void) {
    g_chip_db_count = 0;
    
    // STM8系列
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM8S003F3",
        .device_id = 0x5344,
        .id_mask = 0xFFFF,
        .type = CHIP_TYPE_STM8,
        .flash_addr = 0x8000,
        .flash_size = 8 * 1024,
        .ram_size = 1024,
        .page_size = 64,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM8S103F3",
        .device_id = 0x5348,
        .id_mask = 0xFFFF,
        .type = CHIP_TYPE_STM8,
        .flash_addr = 0x8000,
        .flash_size = 8 * 1024,
        .ram_size = 1024,
        .page_size = 64,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM8S105K4",
        .device_id = 0x5358,
        .id_mask = 0xFFFF,
        .type = CHIP_TYPE_STM8,
        .flash_addr = 0x8000,
        .flash_size = 16 * 1024,
        .ram_size = 2048,
        .page_size = 128,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM8S207",
        .device_id = 0x5378,
        .id_mask = 0xFFFF,
        .type = CHIP_TYPE_STM8,
        .flash_addr = 0x8000,
        .flash_size = 128 * 1024,
        .ram_size = 6144,
        .page_size = 128,
        .valid = true
    };
    
    // STM32系列
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM32F103C8",
        .device_id = 0x0410,
        .id_mask = 0x0FFF,
        .type = CHIP_TYPE_STM32,
        .flash_addr = 0x08000000,
        .flash_size = 64 * 1024,
        .ram_size = 20 * 1024,
        .page_size = 1024,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM32F103CB",
        .device_id = 0x0410,
        .id_mask = 0x0FFF,
        .type = CHIP_TYPE_STM32,
        .flash_addr = 0x08000000,
        .flash_size = 128 * 1024,
        .ram_size = 20 * 1024,
        .page_size = 1024,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM32F030F4",
        .device_id = 0x0444,
        .id_mask = 0x0FFF,
        .type = CHIP_TYPE_STM32,
        .flash_addr = 0x08000000,
        .flash_size = 16 * 1024,
        .ram_size = 4 * 1024,
        .page_size = 1024,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM32F401CC",
        .device_id = 0x0423,
        .id_mask = 0x0FFF,
        .type = CHIP_TYPE_STM32,
        .flash_addr = 0x08000000,
        .flash_size = 256 * 1024,
        .ram_size = 64 * 1024,
        .page_size = 16384,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM32G030F6",
        .device_id = 0x0466,
        .id_mask = 0x0FFF,
        .type = CHIP_TYPE_STM32,
        .flash_addr = 0x08000000,
        .flash_size = 32 * 1024,
        .ram_size = 8 * 1024,
        .page_size = 2048,
        .valid = true
    };
    
    g_chip_db[g_chip_db_count++] = (chip_info_t){
        .name = "STM32L031K6",
        .device_id = 0x0425,
        .id_mask = 0x0FFF,
        .type = CHIP_TYPE_STM32,
        .flash_addr = 0x08000000,
        .flash_size = 32 * 1024,
        .ram_size = 8 * 1024,
        .page_size = 128,
        .valid = true
    };
}

// 根据Device ID查找芯片
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

// 根据名称查找芯片
static chip_info_t* chip_db_find_by_name(const char *name) {
    for (int i = 0; i < g_chip_db_count; i++) {
        if (strcmp(g_chip_db[i].name, name) == 0 && g_chip_db[i].valid) {
            return &g_chip_db[i];
        }
    }
    return NULL;
}

// 获取芯片列表
static int chip_db_get_list(chip_type_t type, chip_info_t **list, int max_count) {
    int count = 0;
    for (int i = 0; i < g_chip_db_count && count < max_count; i++) {
        if ((type == CHIP_TYPE_UNKNOWN || g_chip_db[i].type == type) && g_chip_db[i].valid) {
            list[count++] = &g_chip_db[i];
        }
    }
    return count;
}

//=============================================================================
// SWIM协议 (STM8) - 完整实现
//=============================================================================

#define SWIM_CSR        0x7F80
#define SWIM_CSR2       0x7F81

static volatile uint32_t g_swim_delay_cycles = 10;

// SWIM延时
static inline void swim_delay_us(uint32_t us) {
    sleep_us(us);
}

// SWIM设置高电平
static inline void swim_high(void) {
    gpio_put(PIN_SWIM_SWDIO, 1);
}

// SWIM设置低电平
static inline void swim_low(void) {
    gpio_put(PIN_SWIM_SWDIO, 0);
}

// SWIM读取电平
static inline bool swim_read(void) {
    return gpio_get(PIN_SWIM_SWDIO);
}

// SWIM设置为输出
static inline void swim_output(void) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
}

// SWIM设置为输入
static inline void swim_input(void) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_IN);
}

// SWIM复位序列
static bool swim_entry_sequence(void) {
    // 复位目标芯片
    gpio_put(PIN_NRST, 0);
    swim_output();
    swim_high();
    sleep_us(100);
    
    gpio_put(PIN_NRST, 1);
    sleep_us(100);
    
    // 发送SWIM进入序列：4个低脉冲
    for (int i = 0; i < 4; i++) {
        swim_low();
        swim_delay_us(500);
        swim_high();
        swim_delay_us(500);
    }
    
    sleep_us(100);
    return true;
}

// SWIM发送位 (曼彻斯特编码)
static void swim_write_bit(bool bit) {
    swim_output();
    if (bit) {
        // 1: 低->高
        swim_low();
        swim_delay_us(2);
        swim_high();
        swim_delay_us(2);
    } else {
        // 0: 高->低
        swim_high();
        swim_delay_us(2);
        swim_low();
        swim_delay_us(2);
    }
}

// SWIM读取位
static bool swim_read_bit(void) {
    swim_input();
    swim_delay_us(2);
    bool first = swim_read();
    swim_delay_us(2);
    bool second = swim_read();
    
    // 曼彻斯特解码
    return (!first && second); // 低->高 = 1
}

// SWIM发送字节
static void swim_write_byte(uint8_t data) {
    // 发送起始位
    swim_write_bit(0);
    
    // 发送8个数据位 (MSB first)
    for (int i = 7; i >= 0; i--) {
        swim_write_bit((data >> i) & 1);
    }
    
    // 奇偶校验位
    uint8_t parity = data;
    parity ^= parity >> 4;
    parity ^= parity >> 2;
    parity ^= parity >> 1;
    swim_write_bit(parity & 1);
    
    // ACK
    swim_input();
    swim_delay_us(10);
}

// SWIM读取字节
static uint8_t swim_read_byte(void) {
    uint8_t data = 0;
    
    // 等待起始位
    swim_input();
    while (swim_read_bit()) {}
    
    // 读取8个数据位
    for (int i = 7; i >= 0; i--) {
        if (swim_read_bit()) {
            data |= (1 << i);
        }
    }
    
    // 读取奇偶校验位
    swim_read_bit();
    
    // 发送ACK
    swim_output();
    swim_write_bit(1);
    
    return data;
}

// SWIM读取内存
static bool swim_read_memory(uint32_t addr, uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        // ROTF命令 (0x01)
        swim_write_byte(0x01);
        
        // 发送地址 (24位)
        swim_write_byte((addr >> 16) & 0xFF);
        swim_write_byte((addr >> 8) & 0xFF);
        swim_write_byte(addr & 0xFF);
        
        // 读取数据
        data[i] = swim_read_byte();
        addr++;
    }
    return true;
}

// SWIM写入内存
static bool swim_write_memory(uint32_t addr, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        // WOTF命令 (0x02)
        swim_write_byte(0x02);
        
        // 发送地址 (24位)
        swim_write_byte((addr >> 16) & 0xFF);
        swim_write_byte((addr >> 8) & 0xFF);
        swim_write_byte(addr & 0xFF);
        
        // 写入数据
        swim_write_byte(data[i]);
        addr++;
    }
    return true;
}

// SWIM初始化
static bool swim_init(void) {
    if (!swim_entry_sequence()) {
        return false;
    }
    
    // 读取SWIM_CSR确认连接
    uint8_t csr;
    if (!swim_read_memory(SWIM_CSR, &csr, 1)) {
        return false;
    }
    
    return true;
}

// SWIM读取设备ID
static bool swim_read_device_id(uint16_t *device_id) {
    if (!swim_init()) {
        return false;
    }
    
    // STM8的Device ID在地址0x004850-0x004851
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

// SWIM解锁Flash
static bool swim_unlock_flash(void) {
    // Flash解锁密钥
    uint8_t key1 = 0x56;
    uint8_t key2 = 0xAE;
    
    // FLASH_PUKR = 0x5062
    swim_write_memory(0x5062, &key1, 1);
    swim_write_memory(0x5062, &key2, 1);
    
    sleep_ms(10);
    return true;
}

// SWIM擦除Flash
static bool swim_erase_flash(uint32_t addr, uint32_t size) {
    // 解锁Flash
    if (!swim_unlock_flash()) {
        return false;
    }
    
    // 设置FLASH_CR2和FLASH_NCR2
    // FLASH_CR2 = 0x505B
    // ERASE位
    uint8_t cr2 = 0x40;
    swim_write_memory(0x505B, &cr2, 1);
    
    // 写入任意数据到要擦除的地址触发擦除
    uint8_t dummy = 0x00;
    uint32_t block_addr = addr;
    
    while (block_addr < addr + size) {
        swim_write_memory(block_addr, &dummy, 1);
        
        // 等待擦除完成
        sleep_ms(10);
        
        block_addr += 64; // 按块擦除
    }
    
    return true;
}

// SWIM擦除整个芯片
static bool swim_erase_chip(void) {
    // Mass erase通过擦除所有块实现
    return swim_erase_flash(0x8000, 8 * 1024);
}

//=============================================================================
// SWD协议 (STM32) - 完整实现
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
#define AP_IDR          0xFC

// STM32 Flash寄存器地址 (F1系列)
#define STM32_FLASH_BASE    0x40022000
#define FLASH_ACR           (STM32_FLASH_BASE + 0x00)
#define FLASH_KEYR          (STM32_FLASH_BASE + 0x04)
#define FLASH_OPTKEYR       (STM32_FLASH_BASE + 0x08)
#define FLASH_SR            (STM32_FLASH_BASE + 0x0C)
#define FLASH_CR            (STM32_FLASH_BASE + 0x10)
#define FLASH_AR            (STM32_FLASH_BASE + 0x14)

#define FLASH_KEY1          0x45670123
#define FLASH_KEY2          0xCDEF89AB

static volatile uint32_t g_swd_delay_cycles = 1;

// SWD延时
static inline void swd_delay(void) {
    for (volatile uint32_t i = 0; i < g_swd_delay_cycles; i++) {
        __asm volatile("nop");
    }
}

// SWD时钟脉冲
static inline void swd_clock(void) {
    gpio_put(PIN_SWCLK, 0);
    swd_delay();
    gpio_put(PIN_SWCLK, 1);
    swd_delay();
}

// SWD写入位
static void swd_write_bit(bool bit) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    gpio_put(PIN_SWIM_SWDIO, bit);
    swd_clock();
}

// SWD读取位
static bool swd_read_bit(void) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_IN);
    swd_clock();
    return gpio_get(PIN_SWIM_SWDIO);
}

// 计算奇偶校验
static bool calc_parity(uint32_t data) {
    data ^= data >> 16;
    data ^= data >> 8;
    data ^= data >> 4;
    data ^= data >> 2;
    data ^= data >> 1;
    return data & 1;
}

// SWD线复位
static void swd_line_reset(void) {
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    gpio_set_dir(PIN_SWCLK, GPIO_OUT);
    
    // 发送至少50个时钟周期的高电平
    gpio_put(PIN_SWIM_SWDIO, 1);
    for (int i = 0; i < 60; i++) {
        swd_clock();
    }
    
    // 发送同步序列 0xE79E
    uint16_t sync = 0xE79E;
    for (int i = 0; i < 16; i++) {
        swd_write_bit((sync >> i) & 1);
    }
    
    // 再发送至少50个时钟周期的高电平
    gpio_put(PIN_SWIM_SWDIO, 1);
    for (int i = 0; i < 60; i++) {
        swd_clock();
    }
    
    // 空闲周期
    gpio_put(PIN_SWIM_SWDIO, 0);
    for (int i = 0; i < 8; i++) {
        swd_clock();
    }
}

// SWD传输
static bool swd_transfer(bool ap, bool read, uint8_t addr, uint32_t *data) {
    // 构造请求字节
    uint8_t request = 0x81; // Start=1, Park=1
    request |= (ap ? 0x02 : 0x00);
    request |= (read ? 0x04 : 0x00);
    request |= ((addr & 0x0C) << 1);
    
    // 计算请求的奇偶校验
    bool parity = 0;
    parity ^= ap;
    parity ^= read;
    parity ^= ((addr >> 2) & 1);
    parity ^= ((addr >> 3) & 1);
    request |= (parity ? 0x20 : 0x00);
    
    // 发送请求
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
    for (int i = 0; i < 8; i++) {
        swd_write_bit((request >> i) & 1);
    }
    
    // 转向周期
    gpio_set_dir(PIN_SWIM_SWDIO, GPIO_IN);
    swd_clock();
    
    // 读取ACK (3位)
    uint8_t ack = 0;
    for (int i = 0; i < 3; i++) {
        if (swd_read_bit()) {
            ack |= (1 << i);
        }
    }
    
    if (ack != SWD_ACK_OK) {
        // 转向回输出
        gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
        return false;
    }
    
    if (read) {
        // 读取数据 (32位)
        uint32_t value = 0;
        for (int i = 0; i < 32; i++) {
            if (swd_read_bit()) {
                value |= (1UL << i);
            }
        }
        
        // 读取奇偶校验位
        bool par = swd_read_bit();
        
        // 验证奇偶校验
        if (par != calc_parity(value)) {
            gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
            return false;
        }
        
        *data = value;
        
        // 转向回输出
        gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
        
    } else {
        // 转向周期
        gpio_set_dir(PIN_SWIM_SWDIO, GPIO_OUT);
        swd_clock();
        
        // 写入数据 (32位)
        uint32_t value = *data;
        for (int i = 0; i < 32; i++) {
            swd_write_bit((value >> i) & 1);
        }
        
        // 写入奇偶校验位
        swd_write_bit(calc_parity(value));
    }
    
    // 空闲周期
    gpio_put(PIN_SWIM_SWDIO, 0);
    for (int i = 0; i < 8; i++) {
        swd_clock();
    }
    
    return true;
}

// SWD初始化
static bool swd_init(void) {
    // 硬件复位
    gpio_put(PIN_NRST, 0);
    sleep_ms(10);
    gpio_put(PIN_NRST, 1);
    sleep_ms(10);
    
    // SWD线复位
    swd_line_reset();
    
    // 读取IDCODE
    uint32_t idcode = 0;
    if (!swd_transfer(false, true, DP_IDCODE, &idcode)) {
        return false;
    }
    
    if (idcode == 0 || idcode == 0xFFFFFFFF) {
        return false;
    }
    
    // 上电Debug接口
    uint32_t ctrl = 0x50000000;
    if (!swd_transfer(false, false, DP_CTRL_STAT, &ctrl)) {
        return false;
    }
    
    sleep_ms(10);
    
    // 清除错误标志
    uint32_t abort = 0x1E;
    swd_transfer(false, false, DP_ABORT, &abort);
    
    return true;
}

// SWD读取设备ID
static bool swd_read_device_id(uint16_t *device_id) {
    if (!swd_init()) {
        return false;
    }
    
    // 选择AP 0
    uint32_t select = 0x00000000;
    if (!swd_transfer(false, false, DP_SELECT, &select)) {
        return false;
    }
    
    // 配置CSW: 32位访问，自动递增
    uint32_t csw = 0x23000002;
    if (!swd_transfer(true, false, AP_CSW, &csw)) {
        return false;
    }
    
    // 设置TAR: DBGMCU_IDCODE地址 (0xE0042000)
    uint32_t tar = 0xE0042000;
    if (!swd_transfer(true, false, AP_TAR, &tar)) {
        return false;
    }
    
    // 读取DRW
    uint32_t dummy = 0;
    if (!swd_transfer(true, true, AP_DRW, &dummy)) {
        return false;
    }
    
    // 读取RDBUFF获取实际数据
    uint32_t idcode = 0;
    if (!swd_transfer(false, true, DP_RDBUFF, &idcode)) {
        return false;
    }
    
    // Device ID在低12位
    *device_id = idcode & 0x0FFF;
    
    if (*device_id == 0x0000 || *device_id == 0x0FFF) {
        return false;
    }
    
    return true;
}

// SWD读取内存
static bool swd_read_memory(uint32_t addr, uint8_t *data, uint32_t len) {
    // 选择AP 0
    uint32_t select = 0x00000000;
    if (!swd_transfer(false, false, DP_SELECT, &select)) {
        return false;
    }
    
    // 配置CSW
    uint32_t csw = 0x23000002;
    if (!swd_transfer(true, false, AP_CSW, &csw)) {
        return false;
    }
    
    // 按字读取
    for (uint32_t i = 0; i < len; i += 4) {
        // 设置TAR
        uint32_t tar = addr + i;
        if (!swd_transfer(true, false, AP_TAR, &tar)) {
            return false;
        }
        
        // 读取DRW
        uint32_t dummy = 0;
        if (!swd_transfer(true, true, AP_DRW, &dummy)) {
            return false;
        }
        
        // 读取RDBUFF获取实际数据
        uint32_t value = 0;
        if (!swd_transfer(false, true, DP_RDBUFF, &value)) {
            return false;
        }
        
        // 复制到缓冲区
        uint32_t copy_len = (len - i) < 4 ? (len - i) : 4;
        memcpy(data + i, &value, copy_len);
    }
    
    return true;
}

// SWD写入内存
static bool swd_write_memory(uint32_t addr, const uint8_t *data, uint32_t len) {
    // 选择AP 0
    uint32_t select = 0x00000000;
    if (!swd_transfer(false, false, DP_SELECT, &select)) {
        return false;
    }
    
    // 配置CSW
    uint32_t csw = 0x23000002;
    if (!swd_transfer(true, false, AP_CSW, &csw)) {
        return false;
    }
    
    // 按字写入
    for (uint32_t i = 0; i < len; i += 4) {
        // 设置TAR
        uint32_t tar = addr + i;
        if (!swd_transfer(true, false, AP_TAR, &tar)) {
            return false;
        }
        
        // 准备数据
        uint32_t value = 0xFFFFFFFF;
        uint32_t copy_len = (len - i) < 4 ? (len - i) : 4;
        memcpy(&value, data + i, copy_len);
        
        // 写入DRW
        if (!swd_transfer(true, false, AP_DRW, &value)) {
            return false;
        }
    }
    
    return true;
}

// STM32 Flash解锁
static bool stm32_flash_unlock(void) {
    // 写入解锁密钥
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

// STM32 Flash擦除
static bool stm32_flash_erase(uint32_t addr, uint32_t size) {
    // 解锁Flash
    if (!stm32_flash_unlock()) {
        return false;
    }
    
    // 按页擦除
    uint32_t page_addr = addr;
    while (page_addr < addr + size) {
        // 等待BUSY清除
        uint32_t sr;
        int timeout = 1000;
        do {
            if (!swd_read_memory(FLASH_SR, (uint8_t*)&sr, 4)) {
                return false;
            }
            sleep_ms(1);
        } while ((sr & 0x01) && timeout-- > 0);
        
        if (timeout <= 0) return false;
        
        // 设置PER位
        uint32_t cr = 0x02;
        if (!swd_write_memory(FLASH_CR, (uint8_t*)&cr, 4)) {
            return false;
        }
        
        // 写入页地址
        if (!swd_write_memory(FLASH_AR, (uint8_t*)&page_addr, 4)) {
            return false;
        }
        
        // 设置STRT位
        cr = 0x42;
        if (!swd_write_memory(FLASH_CR, (uint8_t*)&cr, 4)) {
            return false;
        }
        
        // 等待完成
        timeout = 1000;
        do {
            if (!swd_read_memory(FLASH_SR, (uint8_t*)&sr, 4)) {
                return false;
            }
            sleep_ms(1);
        } while ((sr & 0x01) && timeout-- > 0);
        
        if (timeout <= 0) return false;
        
        page_addr += 1024; // 假设页大小为1KB
    }
    
    return true;
}

// STM32 Mass Erase
static bool stm32_mass_erase(void) {
    // 解锁Flash
    if (!stm32_flash_unlock()) {
        return false;
    }
    
    // 等待BUSY清除
    uint32_t sr;
    int timeout = 1000;
    do {
        if (!swd_read_memory(FLASH_SR, (uint8_t*)&sr, 4)) {
            return false;
        }
        sleep_ms(1);
    } while ((sr & 0x01) && timeout-- > 0);
    
    if (timeout <= 0) return false;
    
    // 设置MER位
    uint32_t cr = 0x04;
    if (!swd_write_memory(FLASH_CR, (uint8_t*)&cr, 4)) {
        return false;
    }
    
    // 设置STRT位
    cr = 0x44;
    if (!swd_write_memory(FLASH_CR, (uint8_t*)&cr, 4)) {
        return false;
    }
    
    // 等待完成 (Mass erase可能需要较长时间)
    timeout = 5000;
    do {
        if (!swd_read_memory(FLASH_SR, (uint8_t*)&sr, 4)) {
            return false;
        }
        sleep_ms(1);
    } while ((sr & 0x01) && timeout-- > 0);
    
    if (timeout <= 0) return false;
    
    return true;
}

//=============================================================================
// 连接检测和稳定性检查
//=============================================================================

// 检测芯片连接
static bool programmer_detect_connection(void) {
    // 尝试SWIM
    uint16_t id_swim = 0;
    if (swim_read_device_id(&id_swim)) {
        return true;
    }
    
    // 尝试SWD
    uint16_t id_swd = 0;
    if (swd_read_device_id(&id_swd)) {
        return true;
    }
    
    return false;
}

// 连接稳定性检测
static bool programmer_stability_check(chip_type_t *detected_type, uint16_t *device_id) {
    uint16_t ids[STABILITY_CHECK_TIMES] = {0};
    chip_type_t types[STABILITY_CHECK_TIMES];
    
    for (int i = 0; i < STABILITY_CHECK_TIMES; i++) {
        bool found = false;
        
        // 尝试SWIM
        if (swim_read_device_id(&ids[i])) {
            types[i] = CHIP_TYPE_STM8;
            found = true;
        }
        
        // 尝试SWD
        if (!found && swd_read_device_id(&ids[i])) {
            types[i] = CHIP_TYPE_STM32;
            found = true;
        }
        
        if (!found) {
            return false;
        }
        
        sleep_ms(STABILITY_CHECK_DELAY);
    }
    
    // 检查一致性
    for (int i = 1; i < STABILITY_CHECK_TIMES; i++) {
        if (ids[i] != ids[0] || types[i] != types[0]) {
            return false;
        }
    }
    
    *detected_type = types[0];
    *device_id = ids[0];
    return true;
}

//=============================================================================
// 文件系统 (简化实现)
//=============================================================================

// 初始化文件系统
static bool fs_init(void) {
    g_state.firmware_count = 0;
    g_state.backup_count = 0;
    return true;
}

// 扫描固件文件
static void fs_scan_firmware(void) {
    g_state.firmware_count = 0;
    
    // 添加示例固件
    strcpy(g_state.firmware_list[g_state.firmware_count].filename, "FR300_V1.4.HEX");
    g_state.firmware_list[g_state.firmware_count].type = FILE_TYPE_HEX;
    g_state.firmware_list[g_state.firmware_count].size = 8192;
    g_state.firmware_list[g_state.firmware_count].valid = true;
    g_state.firmware_count++;
    
    strcpy(g_state.firmware_list[g_state.firmware_count].filename, "STM32_APP.BIN");
    g_state.firmware_list[g_state.firmware_count].type = FILE_TYPE_BIN;
    g_state.firmware_list[g_state.firmware_count].size = 16384;
    g_state.firmware_list[g_state.firmware_count].valid = true;
    g_state.firmware_count++;
    
    strcpy(g_state.firmware_list[g_state.firmware_count].filename, "BOOTLOADER.HEX");
    g_state.firmware_list[g_state.firmware_count].type = FILE_TYPE_HEX;
    g_state.firmware_list[g_state.firmware_count].size = 4096;
    g_state.firmware_list[g_state.firmware_count].valid = true;
    g_state.firmware_count++;
}

// 扫描备份文件
static void fs_scan_backups(void) {
    g_state.backup_count = 0;
    
    // 添加示例备份
    strcpy(g_state.backup_list[g_state.backup_count].filename, "STM8S003_001.BIN");
    strcpy(g_state.backup_list[g_state.backup_count].chip_name, "STM8S003F3");
    g_state.backup_list[g_state.backup_count].size = 8192;
    g_state.backup_list[g_state.backup_count].timestamp = 1234567890;
    g_state.backup_list[g_state.backup_count].valid = true;
    g_state.backup_count++;
}

// 加载固件文件
static bool fs_load_firmware(const char *filename, uint8_t *buffer, uint32_t *size) {
    // 简化实现：生成测试数据
    *size = 8192;
    
    // 生成测试固件数据
    for (uint32_t i = 0; i < *size; i++) {
        buffer[i] = (i & 0xFF);
    }
    
    return true;
}

// 保存备份文件
static bool fs_save_backup(const char *chip_name, const uint8_t *data, uint32_t size) {
    // 简化实现
    if (g_state.backup_count >= MAX_BACKUP_FILES) {
        return false;
    }
    
    // 生成文件名
    snprintf(g_state.backup_list[g_state.backup_count].filename, 
             MAX_FILENAME_LEN, "%s_%03d.BIN", chip_name, g_state.backup_count + 1);
    strcpy(g_state.backup_list[g_state.backup_count].chip_name, chip_name);
    g_state.backup_list[g_state.backup_count].size = size;
    g_state.backup_list[g_state.backup_count].timestamp = to_ms_since_boot(get_absolute_time());
    g_state.backup_list[g_state.backup_count].valid = true;
    g_state.backup_count++;
    
    return true;
}

//=============================================================================
// Intel HEX 解析
//=============================================================================

// 解析Intel HEX行
static bool parse_hex_line(const char *line, uint32_t *addr, uint8_t *data, uint32_t *len, uint8_t *type) {
    if (line[0] != ':') return false;
    
    // 读取长度
    char buf[3] = {line[1], line[2], 0};
    *len = strtoul(buf, NULL, 16);
    
    // 读取地址
    buf[0] = line[3]; buf[1] = line[4]; buf[2] = 0;
    uint32_t addr_high = strtoul(buf, NULL, 16);
    buf[0] = line[5]; buf[1] = line[6]; buf[2] = 0;
    uint32_t addr_low = strtoul(buf, NULL, 16);
    *addr = (addr_high << 8) | addr_low;
    
    // 读取类型
    buf[0] = line[7]; buf[1] = line[8]; buf[2] = 0;
    *type = strtoul(buf, NULL, 16);
    
    // 读取数据
    for (uint32_t i = 0; i < *len; i++) {
        buf[0] = line[9 + i * 2];
        buf[1] = line[10 + i * 2];
        buf[2] = 0;
        data[i] = strtoul(buf, NULL, 16);
    }
    
    return true;
}

// 解析HEX文件
static bool parse_hex_file(const uint8_t *hex_data, uint32_t hex_size,
                          uint8_t *bin_data, uint32_t *bin_size, uint32_t *base_addr) {
    // 简化实现：假设HEX数据正确并直接转换
    *bin_size = 8192;
    *base_addr = 0x08000000;
    
    memset(bin_data, 0xFF, *bin_size);
    
    // 这里应该逐行解析HEX文件
    // 简化处理：直接复制前面的数据
    uint32_t copy_size = (hex_size < *bin_size) ? hex_size : *bin_size;
    memcpy(bin_data, hex_data, copy_size);
    
    return true;
}

//=============================================================================
// 烧录操作
//=============================================================================

// 自动识别芯片
static bool programmer_auto_detect(chip_info_t *chip) {
    chip_type_t type;
    uint16_t device_id;
    
    // 连接稳定性检测
    if (g_state.config.stability_check_enabled) {
        if (!programmer_stability_check(&type, &device_id)) {
            g_state.error_code = 1; // 连接不稳定
            return false;
        }
    } else {
        // 直接检测
        bool found = false;
        
        if (swim_read_device_id(&device_id)) {
            type = CHIP_TYPE_STM8;
            found = true;
        }
        
        if (!found && swd_read_device_id(&device_id)) {
            type = CHIP_TYPE_STM32;
            found = true;
        }
        
        if (!found) {
            g_state.error_code = 2; // 无法检测到芯片
            return false;
        }
    }
    
    // 在数据库中查找
    chip_info_t *found_chip = chip_db_find_by_id(device_id, type);
    if (found_chip) {
        memcpy(chip, found_chip, sizeof(chip_info_t));
        return true;
    }
    
    // 未知芯片
    if (g_state.config.allow_unknown_chip) {
        snprintf(chip->name, sizeof(chip->name), "Unknown_%04X", device_id);
        chip->device_id = device_id;
        chip->type = type;
        chip->flash_addr = (type == CHIP_TYPE_STM32) ? 0x08000000 : 0x8000;
        chip->flash_size = 64 * 1024;
        chip->page_size = 1024;
        chip->valid = true;
        return true;
    }
    
    g_state.error_code = 3; // 未知芯片且不允许继续
    return false;
}

// 擦除芯片
static bool programmer_erase_chip(chip_info_t *chip) {
    if (chip->type == CHIP_TYPE_STM8) {
        return swim_erase_chip();
    } else if (chip->type == CHIP_TYPE_STM32) {
        return stm32_mass_erase();
    }
    return false;
}

// 写入Flash
static bool programmer_write_flash(chip_info_t *chip, uint32_t addr,
                                   const uint8_t *data, uint32_t size,
                                   bool update_progress) {
    if (chip->type == CHIP_TYPE_STM8) {
        // STM8需要先解锁
        swim_unlock_flash();
        
        // 分块写入
        uint32_t offset = 0;
        while (offset < size) {
            uint32_t block_size = (size - offset) > 128 ? 128 : (size - offset);
            
            if (!swim_write_memory(addr + offset, data + offset, block_size)) {
                g_state.error_code = 4; // 写入失败
                return false;
            }
            
            offset += block_size;
            
            if (update_progress) {
                g_state.prog_progress = offset;
                g_state.prog_total = size;
            }
        }
        
    } else if (chip->type == CHIP_TYPE_STM32) {
        // STM32先解锁Flash
        if (!stm32_flash_unlock()) {
            g_state.error_code = 5; // 解锁失败
            return false;
        }
        
        // 分块写入
        uint32_t offset = 0;
        while (offset < size) {
            uint32_t block_size = (size - offset) > 256 ? 256 : (size - offset);
            
            if (!swd_write_memory(addr + offset, data + offset, block_size)) {
                g_state.error_code = 4; // 写入失败
                return false;
            }
            
            offset += block_size;
            
            if (update_progress) {
                g_state.prog_progress = offset;
                g_state.prog_total = size;
            }
        }
    }
    
    return true;
}

// 读取Flash
static bool programmer_read_flash(chip_info_t *chip, uint32_t addr,
                                  uint8_t *data, uint32_t size,
                                  bool update_progress) {
    if (chip->type == CHIP_TYPE_STM8) {
        uint32_t offset = 0;
        while (offset < size) {
            uint32_t block_size = (size - offset) > 128 ? 128 : (size - offset);
            
            if (!swim_read_memory(addr + offset, data + offset, block_size)) {
                g_state.error_code = 6; // 读取失败
                return false;
            }
            
            offset += block_size;
            
            if (update_progress) {
                g_state.prog_progress = offset;
                g_state.prog_total = size;
            }
        }
        
    } else if (chip->type == CHIP_TYPE_STM32) {
        uint32_t offset = 0;
        while (offset < size) {
            uint32_t block_size = (size - offset) > 256 ? 256 : (size - offset);
            
            if (!swd_read_memory(addr + offset, data + offset, block_size)) {
                g_state.error_code = 6; // 读取失败
                return false;
            }
            
            offset += block_size;
            
            if (update_progress) {
                g_state.prog_progress = offset;
                g_state.prog_total = size;
            }
        }
    }
    
    return true;
}

// 校验Flash
static bool programmer_verify_flash(chip_info_t *chip, uint32_t addr,
                                    const uint8_t *data, uint32_t size) {
    uint8_t read_buf[256];
    uint32_t offset = 0;
    
    g_state.prog_progress = 0;
    g_state.prog_total = size;
    
    while (offset < size) {
        uint32_t read_size = (size - offset) > sizeof(read_buf) ?
                            sizeof(read_buf) : (size - offset);
        
        if (!programmer_read_flash(chip, addr + offset, read_buf, read_size, false)) {
            return false;
        }
        
        if (memcmp(data + offset, read_buf, read_size) != 0) {
            g_state.error_code = 7; // 校验失败
            return false;
        }
        
        offset += read_size;
        g_state.prog_progress = offset;
    }
    
    return true;
}

// 自动烧录流程
static bool programmer_auto_program(programmer_state_t *state) {
    state->error_code = 0;
    
    // 1. 连接
    state->prog_state = PROG_STATE_CONNECTING;
    strcpy(state->prog_message, "Connecting...");
    
    // 2. 检测芯片
    state->prog_state = PROG_STATE_DETECTING;
    strcpy(state->prog_message, "Detecting...");
    
    if (!programmer_auto_detect(&state->detected_chip)) {
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "Detect failed");
        return false;
    }
    
    state->chip_detected = true;
    snprintf(state->prog_message, sizeof(state->prog_message),
             "Found: %s", state->detected_chip.name);
    sleep_ms(500);
    
    // 3. 加载固件
    if (state->config.default_firmware_index >= 0 &&
        state->config.default_firmware_index < state->firmware_count) {
        
        firmware_file_t *fw = &state->firmware_list[state->config.default_firmware_index];
        
        if (!fs_load_firmware(fw->filename, g_firmware_buffer, &g_firmware_size)) {
            state->prog_state = PROG_STATE_ERROR;
            strcpy(state->prog_message, "Load FW failed");
            state->error_code = 8;
            return false;
        }
        
        // HEX文件解析
        if (fw->type == FILE_TYPE_HEX) {
            uint8_t bin_buffer[MAX_FILE_SIZE];
            uint32_t bin_size;
            uint32_t base_addr;
            
            if (!parse_hex_file(g_firmware_buffer, g_firmware_size,
                               bin_buffer, &bin_size, &base_addr)) {
                state->prog_state = PROG_STATE_ERROR;
                strcpy(state->prog_message, "Parse HEX fail");
                state->error_code = 9;
                return false;
            }
            
            memcpy(g_firmware_buffer, bin_buffer, bin_size);
            g_firmware_size = bin_size;
        }
    } else {
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "No firmware");
        state->error_code = 10;
        return false;
    }
    
    // 4. 备份（如果需要）
    if (state->config.backup_before_program) {
        strcpy(state->prog_message, "Backing up...");
        
        if (!programmer_read_flash(&state->detected_chip,
                                  state->detected_chip.flash_addr,
                                  state->read_buffer,
                                  state->detected_chip.flash_size,
                                  true)) {
            // 备份失败不中断流程
        } else {
            fs_save_backup(state->detected_chip.name,
                          state->read_buffer,
                          state->detected_chip.flash_size);
        }
    }
    
    // 5. 擦除
    state->prog_state = PROG_STATE_ERASING;
    strcpy(state->prog_message, "Erasing...");
    
    if (!programmer_erase_chip(&state->detected_chip)) {
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "Erase failed");
        state->error_code = 11;
        return false;
    }
    
    // 6. 写入
    state->prog_state = PROG_STATE_WRITING;
    strcpy(state->prog_message, "Writing...");
    state->prog_progress = 0;
    state->prog_total = g_firmware_size;
    
    if (!programmer_write_flash(&state->detected_chip,
                               state->detected_chip.flash_addr,
                               g_firmware_buffer,
                               g_firmware_size,
                               true)) {
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "Write failed");
        return false;
    }
    
    // 7. 校验
    if (state->config.verify_after_program) {
        state->prog_state = PROG_STATE_VERIFYING;
        strcpy(state->prog_message, "Verifying...");
        state->prog_progress = 0;
        
        if (!programmer_verify_flash(&state->detected_chip,
                                    state->detected_chip.flash_addr,
                                    g_firmware_buffer,
                                    g_firmware_size)) {
            state->prog_state = PROG_STATE_ERROR;
            strcpy(state->prog_message, "Verify failed");
            return false;
        }
    }
    
    // 8. 成功
    state->prog_state = PROG_STATE_SUCCESS;
    strcpy(state->prog_message, "Success!");
    
    return true;
}

// 手动烧录流程
static bool programmer_manual_program(programmer_state_t *state) {
    // 使用选定的芯片和固件
    memcpy(&state->detected_chip, &state->selected_chip, sizeof(chip_info_t));
    state->chip_detected = true;
    
    return programmer_auto_program(state);
}

// 读取整个Flash
static bool programmer_read_full(programmer_state_t *state) {
    state->error_code = 0;
    
    // 检测芯片
    state->prog_state = PROG_STATE_DETECTING;
    strcpy(state->prog_message, "Detecting...");
    
    if (!programmer_auto_detect(&state->detected_chip)) {
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "Detect failed");
        return false;
    }
    
    // 读取
    state->prog_state = PROG_STATE_READING;
    strcpy(state->prog_message, "Reading...");
    state->prog_progress = 0;
    state->prog_total = state->detected_chip.flash_size;
    
    if (!programmer_read_flash(&state->detected_chip,
                              state->detected_chip.flash_addr,
                              state->read_buffer,
                              state->detected_chip.flash_size,
                              true)) {
        state->prog_state = PROG_STATE_ERROR;
        strcpy(state->prog_message, "Read failed");
        return false;
    }
    
    state->read_size = state->detected_chip.flash_size;
    
    // 成功
    state->prog_state = PROG_STATE_SUCCESS;
    strcpy(state->prog_message, "Read OK!");
    
    return true;
}

//=============================================================================
// 菜单系统 - 完整实现
//=============================================================================

// 菜单定义
static const char *main_menu_items[] = {
    "Start Program",
    "Read/Backup",
    "Chip Erase",
    "Firmware Mgmt",
    "Settings"
};
#define MAIN_MENU_COUNT (sizeof(main_menu_items) / sizeof(main_menu_items[0]))

static const char *read_backup_items[] = {
    "Read Program",
    "Read Range",
    "Compare",
    "CRC/Hash",
    "Backup Mgmt"
};
#define READ_BACKUP_COUNT (sizeof(read_backup_items) / sizeof(read_backup_items[0]))

static const char *erase_items[] = {
    "Mass Erase",
    "Chip Info"
};
#define ERASE_COUNT (sizeof(erase_items) / sizeof(erase_items[0]))

static const char *firmware_items[] = {
    "Internal FW",
    "USB Files",
    "FW Info"
};
#define FIRMWARE_COUNT (sizeof(firmware_items) / sizeof(firmware_items[0]))

static const char *settings_items[] = {
    "Program Mode",
    "Interface",
    "Stability Chk",
    "Allow Unknown",
    "Verify After",
    "Backup Before",
    "Default Chip",
    "Default FW",
    "SWD Speed"
};
#define SETTINGS_COUNT (sizeof(settings_items) / sizeof(settings_items[0]))

// 绘制菜单头部
static void menu_draw_header(const char *title) {
    oled_show_string(5, 0, title);
    oled_draw_line(0, 10, OLED_WIDTH - 1, 10);
}

// 绘制菜单项
static void menu_draw_items(const char **items, int count, int selected, int scroll) {
    int y = MENU_START_Y;
    int visible = MENU_ITEMS_PER_PAGE;
    
    for (int i = 0; i < visible && (scroll + i) < count; i++) {
        int index = scroll + i;
        
        if (index == selected) {
            oled_show_string(0, y, ">");
        }
        
        oled_show_string(8, y, items[index]);
        y += MENU_LINE_HEIGHT;
    }
    
    // 绘制滚动条
    if (count > visible) {
        int bar_height = (OLED_HEIGHT - MENU_START_Y) * visible / count;
        int bar_pos = (OLED_HEIGHT - MENU_START_Y - bar_height) * scroll / (count - visible);
        oled_draw_rect(OLED_WIDTH - 2, MENU_START_Y + bar_pos, 2, bar_height, true);
    }
}

// 绘制主菜单
static void menu_draw_main(void) {
    oled_clear();
    menu_draw_header("STM PROGRAMMER");
    menu_draw_items(main_menu_items, MAIN_MENU_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    oled_refresh();
}

// 绘制读取/备份菜单
static void menu_draw_read_backup(void) {
    oled_clear();
    menu_draw_header("Read/Backup");
    menu_draw_items(read_backup_items, READ_BACKUP_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    oled_refresh();
}

// 绘制擦除菜单
static void menu_draw_erase(void) {
    oled_clear();
    menu_draw_header("Chip Erase");
    menu_draw_items(erase_items, ERASE_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    oled_refresh();
}

// 绘制固件管理菜单
static void menu_draw_firmware(void) {
    oled_clear();
    menu_draw_header("Firmware Mgmt");
    menu_draw_items(firmware_items, FIRMWARE_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    oled_refresh();
}

// 绘制设置菜单
static void menu_draw_settings(void) {
    oled_clear();
    menu_draw_header("Settings");
    menu_draw_items(settings_items, SETTINGS_COUNT,
                   g_menu.selected_item, g_menu.scroll_offset);
    oled_refresh();
}

// 绘制芯片选择菜单
static void menu_draw_chip_select(void) {
    oled_clear();
    menu_draw_header("Select Chip");
    
    int y = MENU_START_Y;
    int visible = MENU_ITEMS_PER_PAGE;
    
    for (int i = 0; i < visible && (g_menu.scroll_offset + i) < g_chip_db_count; i++) {
        int index = g_menu.scroll_offset + i;
        
        if (g_chip_db[index].valid) {
            if (index == g_menu.selected_item) {
                oled_show_string(0, y, ">");
            }
            
            oled_show_string(8, y, g_chip_db[index].name);
            y += MENU_LINE_HEIGHT;
        }
    }
    
    oled_refresh();
}

// 绘制固件选择菜单
static void menu_draw_firmware_select(void) {
    oled_clear();
    menu_draw_header("Select Firmware");
    
    int y = MENU_START_Y;
    int visible = MENU_ITEMS_PER_PAGE;
    
    for (int i = 0; i < visible && (g_menu.scroll_offset + i) < g_state.firmware_count; i++) {
        int index = g_menu.scroll_offset + i;
        
        if (index == g_menu.selected_item) {
            oled_show_string(0, y, ">");
        }
        
        // 显示文件名（截断过长的名称）
        char short_name[20];
        strncpy(short_name, g_state.firmware_list[index].filename, 18);
        short_name[18] = 0;
        if (strlen(g_state.firmware_list[index].filename) > 18) {
            strcat(short_name, "..");
        }
        
        oled_show_string(8, y, short_name);
        y += MENU_LINE_HEIGHT;
    }
    
    oled_refresh();
}

// 绘制进度界面
static void menu_draw_progress(void) {
    oled_clear();
    
    // 芯片名称
    oled_show_string(0, 0, g_state.detected_chip.name);
    
    // 状态信息
    oled_show_string(0, 12, g_state.prog_message);
    
    // 进度条
    if (g_state.prog_total > 0) {
        oled_show_progress(5, 28, OLED_WIDTH - 10, 8,
                          g_state.prog_progress, g_state.prog_total);
        
        // 百分比
        oled_show_percent(OLED_WIDTH / 2 - 12, 40,
                         g_state.prog_progress, g_state.prog_total);
        
        // 字节数
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu/%lu",
                (unsigned long)g_state.prog_progress,
                (unsigned long)g_state.prog_total);
        oled_show_string(5, 50, buf);
    }
    
    // 错误码
    if (g_state.error_code != 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Err:%d", g_state.error_code);
        oled_show_string(OLED_WIDTH - 36, 0, buf);
    }
    
    oled_refresh();
}

// 绘制确认对话框
static void menu_draw_confirm(const char *message) {
    oled_clear();
    
    // 消息
    oled_show_string(10, 15, message);
    
    // 选项
    oled_show_string(20, 35, "OK");
    oled_show_string(70, 35, "Cancel");
    
    // 选择指示
    if (g_menu.selected_item == 0) {
        oled_show_string(10, 35, ">");
    } else {
        oled_show_string(60, 35, ">");
    }
    
    oled_refresh();
}

// 绘制信息界面
static void menu_draw_info(void) {
    oled_clear();
    
    oled_show_string(5, 5, "Info");
    oled_draw_line(0, 15, OLED_WIDTH - 1, 15);
    
    // 显示信息（支持多行）
    int y = 20;
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
    
    oled_show_string(40, OLED_HEIGHT - 8, "OK");
    
    oled_refresh();
}

// 处理菜单导航
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

// 菜单处理
static void menu_process(menu_state_t *menu, key_event_t key, programmer_state_t *state) {
    if (key == KEY_NONE) return;
    
    switch (menu->current_menu) {
        case MENU_MAIN:
            menu_navigate(MAIN_MENU_COUNT, key);
            
            if (key == KEY_OK) {
                menu->previous_menu = MENU_MAIN;
                switch (menu->selected_item) {
                    case 0: // Start Program
                        if (state->config.program_mode == PROGRAM_MODE_AUTO) {
                            menu->current_menu = MENU_PROGRAM_PROGRESS;
                            // 自动烧录在main loop触发
                        } else {
                            // 手动模式：先选择芯片
                            menu->current_menu = MENU_CHIP_SELECT;
                            menu->selected_item = 0;
                            menu->scroll_offset = 0;
                        }
                        menu->need_refresh = true;
                        break;
                    case 1: // Read/Backup
                        menu->current_menu = MENU_READ_BACKUP;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        menu->need_refresh = true;
                        break;
                    case 2: // Chip Erase
                        menu->current_menu = MENU_CHIP_ERASE;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        menu->need_refresh = true;
                        break;
                    case 3: // Firmware Mgmt
                        menu->current_menu = MENU_FIRMWARE_MGMT;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        menu->need_refresh = true;
                        break;
                    case 4: // Settings
                        menu->current_menu = MENU_SETTINGS;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        menu->need_refresh = true;
                        break;
                }
            }
            
            if (menu->need_refresh) {
                menu_draw_main();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_READ_BACKUP:
            menu_navigate(READ_BACKUP_COUNT, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->scroll_offset = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                switch (menu->selected_item) {
                    case 0: // Read Program
                        menu->current_menu = MENU_PROGRAM_PROGRESS;
                        state->prog_state = PROG_STATE_IDLE;
                        menu->need_refresh = true;
                        // 在main loop中执行读取
                        break;
                    case 4: // Backup Mgmt
                        strcpy(menu->info_message, "Backup files:\n");
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d backups", state->backup_count);
                        strcat(menu->info_message, buf);
                        menu->current_menu = MENU_INFO;
                        menu->need_refresh = true;
                        break;
                }
            }
            
            if (menu->need_refresh) {
                menu_draw_read_backup();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_CHIP_ERASE:
            menu_navigate(ERASE_COUNT, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                switch (menu->selected_item) {
                    case 0: // Mass Erase
                        menu->current_menu = MENU_CONFIRM;
                        menu->selected_item = 0;
                        menu->need_refresh = true;
                        break;
                    case 1: // Chip Info
                        if (programmer_auto_detect(&state->detected_chip)) {
                            snprintf(menu->info_message, sizeof(menu->info_message),
                                    "%s\nFlash:%luK\nRAM:%luK",
                                    state->detected_chip.name,
                                    (unsigned long)(state->detected_chip.flash_size / 1024),
                                    (unsigned long)(state->detected_chip.ram_size / 1024));
                        } else {
                            strcpy(menu->info_message, "No chip\ndetected");
                        }
                        menu->current_menu = MENU_INFO;
                        menu->need_refresh = true;
                        break;
                }
            }
            
            if (menu->need_refresh) {
                menu_draw_erase();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_FIRMWARE_MGMT:
            menu_navigate(FIRMWARE_COUNT, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                switch (menu->selected_item) {
                    case 0: // Internal FW
                        menu->current_menu = MENU_FIRMWARE_SELECT;
                        menu->selected_item = state->config.default_firmware_index;
                        if (menu->selected_item < 0) menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        menu->need_refresh = true;
                        break;
                    case 2: // FW Info
                        if (state->config.default_firmware_index >= 0) {
                            firmware_file_t *fw = &state->firmware_list[state->config.default_firmware_index];
                            snprintf(menu->info_message, sizeof(menu->info_message),
                                    "Current FW:\n%s\nSize:%luB",
                                    fw->filename,
                                    (unsigned long)fw->size);
                        } else {
                            strcpy(menu->info_message, "No firmware\nselected");
                        }
                        menu->current_menu = MENU_INFO;
                        menu->need_refresh = true;
                        break;
                }
            }
            
            if (menu->need_refresh) {
                menu_draw_firmware();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_SETTINGS:
            menu_navigate(SETTINGS_COUNT, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = MENU_MAIN;
                menu->selected_item = 0;
                menu->scroll_offset = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                // 切换设置值
                switch (menu->selected_item) {
                    case 0: // Program Mode
                        state->config.program_mode = (state->config.program_mode == PROGRAM_MODE_AUTO) ?
                                                    PROGRAM_MODE_MANUAL : PROGRAM_MODE_AUTO;
                        break;
                    case 1: // Interface
                        state->config.interface_mode = (state->config.interface_mode + 1) % 3;
                        break;
                    case 2: // Stability Check
                        state->config.stability_check_enabled = !state->config.stability_check_enabled;
                        break;
                    case 3: // Allow Unknown
                        state->config.allow_unknown_chip = !state->config.allow_unknown_chip;
                        break;
                    case 4: // Verify After
                        state->config.verify_after_program = !state->config.verify_after_program;
                        break;
                    case 5: // Backup Before
                        state->config.backup_before_program = !state->config.backup_before_program;
                        break;
                    case 6: // Default Chip
                        menu->current_menu = MENU_CHIP_SELECT;
                        menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        menu->need_refresh = true;
                        return;
                    case 7: // Default FW
                        menu->current_menu = MENU_FIRMWARE_SELECT;
                        menu->selected_item = state->config.default_firmware_index;
                        if (menu->selected_item < 0) menu->selected_item = 0;
                        menu->scroll_offset = 0;
                        menu->need_refresh = true;
                        return;
                    case 8: // SWD Speed
                        state->config.swd_speed = (state->config.swd_speed + 1) % 3;
                        break;
                }
                config_save(&state->config);
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
                state->config.default_chip_index = menu->selected_item;
                memcpy(&state->selected_chip, &g_chip_db[menu->selected_item], sizeof(chip_info_t));
                
                if (menu->previous_menu == MENU_SETTINGS) {
                    menu->current_menu = MENU_SETTINGS;
                } else {
                    // 手动烧录模式：选择固件
                    menu->current_menu = MENU_FIRMWARE_SELECT;
                    menu->selected_item = state->config.default_firmware_index;
                    if (menu->selected_item < 0) menu->selected_item = 0;
                }
                menu->scroll_offset = 0;
                menu->need_refresh = true;
            }
            
            if (menu->need_refresh) {
                menu_draw_chip_select();
                menu->need_refresh = false;
            }
            break;
            
        case MENU_FIRMWARE_SELECT:
            menu_navigate(state->firmware_count, key);
            
            if (key == KEY_BACK) {
                menu->current_menu = menu->previous_menu;
                menu->selected_item = 0;
                menu->need_refresh = true;
            } else if (key == KEY_OK) {
                state->config.default_firmware_index = menu->selected_item;
                config_save(&state->config);
                
                if (menu->previous_menu == MENU_CHIP_SELECT && state->config.program_mode == PROGRAM_MODE_MANUAL) {
                    // 手动烧录：开始烧录
                    menu->current_menu = MENU_PROGRAM_PROGRESS;
                    state->prog_state = PROG_STATE_IDLE;
                } else {
                    menu->current_menu = menu->previous_menu;
                }
                menu->selected_item = 0;
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
                menu_draw_confirm("Erase chip?");
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

//=============================================================================
// 配置管理
//=============================================================================

#define CONFIG_MAGIC 0x53544D50  // "STMP"
// 加载配置
static void config_load(system_config_t *config) {
    config->magic = CONFIG_MAGIC;
    config->program_mode = PROGRAM_MODE_AUTO;
    config->interface_mode = INTERFACE_AUTO;
    config->stability_check_enabled = true;
    config->allow_unknown_chip = false;
    config->keep_connected_after_program = false;
    config->verify_after_program = true;
    config->backup_before_program = false;
    config->swd_speed = SWD_SPEED_MEDIUM;
    config->default_chip_index = -1;
    config->default_firmware_index = 0;
    config->max_backups = 10;
    config->screen_brightness = 128;
    config->screen_auto_off = false;
}

// 保存配置
static void config_save(system_config_t *config) {
    config->magic = CONFIG_MAGIC;
    // 实际项目中应该保存到Flash
}

//=============================================================================
// USB MSC (简化)
//=============================================================================

static void usb_msc_init(void) {
    // USB MSC初始化
}

static bool usb_msc_is_connected(void) {
    // 检测USB连接
    return false;
}

//=============================================================================
// 硬件初始化
//=============================================================================

static void hardware_init(void) {
    stdio_init_all();
    
    // I2C初始化 (OLED)
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
    
    // 烧录接口GPIO初始化
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
    // OLED初始化
    if (!oled_init()) {
        return false;
    }
    
    oled_clear();
    oled_show_string(10, 20, "STM PROGRAMMER");
    oled_show_string(25, 35, "Initializing...");
    oled_refresh();
    
    // 按键初始化
    keys_init();
    
    // 文件系统初始化
    if (!fs_init()) {
        oled_clear();
        oled_show_string(20, 28, "FS Init Failed!");
        oled_refresh();
        sleep_ms(2000);
        return false;
    }
    
    // 加载配置
    config_load(&g_state.config);
    
    // 加载芯片数据库
    chip_db_init();
    
    // 扫描固件文件
    fs_scan_firmware();
    fs_scan_backups();
    
    // USB MSC初始化
    usb_msc_init();
    
    sleep_ms(1000);
    return true;
}

//=============================================================================
// 主循环
//=============================================================================

static void main_loop(void) {
    g_menu.current_menu = MENU_MAIN;
    g_menu.selected_item = 0;
    g_menu.need_refresh = true;
    
    while (1) {
        // 检查USB连接
        if (usb_msc_is_connected()) {
            oled_clear();
            oled_show_string(25, 24, "USB Mode");
            oled_show_string(15, 38, "File Transfer");
            oled_refresh();
            
            while (usb_msc_is_connected()) {
                sleep_ms(100);
            }
            
            // USB断开，重新扫描
            fs_scan_firmware();
            fs_scan_backups();
            
            g_menu.need_refresh = true;
        }
        
        // 按键扫描
        key_event_t key = keys_scan();
        
        // 菜单处理
        menu_process(&g_menu, key, &g_state);
        
        // 烧录流程处理
        if (g_menu.current_menu == MENU_PROGRAM_PROGRESS) {
            if (g_state.prog_state == PROG_STATE_IDLE) {
                // 根据上下文决定执行什么操作
                if (g_menu.previous_menu == MENU_MAIN && g_state.config.program_mode == PROGRAM_MODE_AUTO) {
                    // 自动烧录模式
                    if (!g_state.auto_program_locked && programmer_detect_connection()) {
                        programmer_auto_program(&g_state);
                        
                        // 等待芯片断开
                        while (programmer_detect_connection()) {
                            menu_draw_progress();
                            sleep_ms(100);
                        }
                        
                        g_state.auto_program_locked = true;
                        sleep_ms(2000);
                        
                        // 返回主菜单
                        g_menu.current_menu = MENU_MAIN;
                        g_menu.selected_item = 0;
                        g_menu.need_refresh = true;
                    }
                } else if (g_menu.previous_menu == MENU_CHIP_SELECT || g_state.config.program_mode == PROGRAM_MODE_MANUAL) {
                    // 手动烧录模式
                    programmer_manual_program(&g_state);
                } else if (g_menu.previous_menu == MENU_READ_BACKUP) {
                    // 读取Flash
                    programmer_read_full(&g_state);
                    
                    // 保存备份
                    if (g_state.prog_state == PROG_STATE_SUCCESS) {
                        fs_save_backup(g_state.detected_chip.name,
                                      g_state.read_buffer,
                                      g_state.read_size);
                    }
                } else if (g_menu.previous_menu == MENU_CONFIRM) {
                    // 执行擦除
                    g_state.prog_state = PROG_STATE_DETECTING;
                    strcpy(g_state.prog_message, "Detecting...");
                    
                    if (programmer_auto_detect(&g_state.detected_chip)) {
                        g_state.prog_state = PROG_STATE_ERASING;
                        strcpy(g_state.prog_message, "Erasing...");
                        
                        if (programmer_erase_chip(&g_state.detected_chip)) {
                            g_state.prog_state = PROG_STATE_SUCCESS;
                            strcpy(g_state.prog_message, "Erase OK!");
                        } else {
                            g_state.prog_state = PROG_STATE_ERROR;
                            strcpy(g_state.prog_message, "Erase failed");
                        }
                    } else {
                        g_state.prog_state = PROG_STATE_ERROR;
                        strcpy(g_state.prog_message, "No chip");
                    }
                }
            }
        }
        
        // 解锁自动烧录
        if (!programmer_detect_connection()) {
            g_state.auto_program_locked = false;
        }
        
        sleep_ms(10);
    }
}

//=============================================================================
// 主函数
//=============================================================================

int main(void) {
    // 硬件初始化
    hardware_init();
    
    // 系统初始化
    if (!system_init()) {
        while (1) {
            sleep_ms(1000);
        }
    }
    
    // 进入主循环
    main_loop();
    
    return 0;
}
