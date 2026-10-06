// stm_programmer_main.c
// 完整正式版 - 主程序
// 
// 编译说明：
// 1. 将本文件、font_data.c、menu_system.c 放在同一目录
// 2. 使用 CMakeLists.txt 编译
// 3. 烧录到 RP2040-Zero

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


// main.c
#include "stm_programmer.h"

// 全局变量定义（注意：不是extern）
programmer_state_t g_state = {0};
menu_state_t g_menu = {0};
chip_info_t g_chip_db[MAX_CHIP_DB] = {0};
int g_chip_db_count = 0;
uint8_t g_oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8] = {0};
uint8_t g_stream_buffer[STREAM_BUFFER_SIZE] __attribute__((aligned(4)));
//=============================================================================
// 配置定义
//=============================================================================

// GPIO定义
#define PIN_OLED_SCL    29
#define PIN_OLED_SDA    28
#define PIN_KEY1        27  // 上
#define PIN_KEY2        26  // 下
#define PIN_KEY3        15  // 确认
#define PIN_KEY4        14  // 返回
#define PIN_SWIM_SWDIO  2
#define PIN_SWCLK       3
#define PIN_NRST        4

// OLED参数
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDR       0x3C

// Flash布局
#define FLASH_SIZE              (2 * 1024 * 1024)
#define CONFIG_OFFSET           0x100000
#define CONFIG_SIZE             (4 * 1024)
#define FONT_OFFSET             0x101000
#define FONT_SIZE               (192 * 1024)
#define FILESYSTEM_OFFSET       0x131000
#define FILESYSTEM_SIZE         (828 * 1024)

// 缓冲区
#define STREAM_BUFFER_SIZE      (4 * 1024)
#define STM8_WRITE_BLOCK        128
#define STM32_WRITE_BLOCK       256
#define MAX_FILENAME_LEN        64
#define MAX_PATH_LEN            128
#define MAX_FILES               64
#define MAX_CHIP_DB             128

// 按键
#define KEY_DEBOUNCE_MS         50
#define KEY_REPEAT_MS           500
#define KEY_REPEAT_FAST_MS      100

// 超时
#define CONNECT_TIMEOUT_MS      1000
#define STABILITY_CHECK_TIMES   3

// 错误码
#define ERR_OK                  0
#define ERR_CONNECTION_UNSTABLE 1
#define ERR_NO_CHIP_DETECTED    2
#define ERR_UNKNOWN_CHIP        3
#define ERR_WRITE_FAILED        4
#define ERR_UNLOCK_FAILED       5
#define ERR_READ_FAILED         6
#define ERR_VERIFY_FAILED       7
#define ERR_LOAD_FIRMWARE       8
#define ERR_PARSE_HEX           9
#define ERR_NO_FIRMWARE         10
#define ERR_ERASE_FAILED        11
#define ERR_TIMEOUT             12
#define ERR_FILE_NOT_FOUND      13
#define ERR_FILE_TOO_LARGE      14
#define ERR_FS_ERROR            15
#define ERR_INVALID_PARAM       16

//=============================================================================
// 数据类型
//=============================================================================

typedef enum {
    KEY_NONE = 0, KEY_UP, KEY_DOWN, KEY_OK, KEY_BACK
} key_event_t;

typedef enum {
    CHIP_TYPE_UNKNOWN = 0, CHIP_TYPE_STM8, CHIP_TYPE_STM32
} chip_type_t;

typedef enum {
    FILE_TYPE_BIN = 0, FILE_TYPE_HEX, FILE_TYPE_S19
} file_type_t;

typedef enum {
    INTERFACE_AUTO = 0, INTERFACE_SWIM, INTERFACE_SWD
} interface_mode_t;

typedef enum {
    PROGRAM_MODE_AUTO = 0, PROGRAM_MODE_MANUAL
} program_mode_t;

typedef enum {
    SWD_SPEED_LOW = 0, SWD_SPEED_MEDIUM, SWD_SPEED_HIGH
} swd_speed_t;

typedef enum {
    PROG_STATE_IDLE = 0,
    PROG_STATE_CONNECTING,
    PROG_STATE_DETECTING,
    PROG_STATE_LOADING,
    PROG_STATE_BACKING_UP,
    PROG_STATE_ERASING,
    PROG_STATE_WRITING,
    PROG_STATE_VERIFYING,
    PROG_STATE_READING,
    PROG_STATE_SUCCESS,
    PROG_STATE_ERROR
} prog_state_t;

typedef enum {
    MENU_MAIN = 0,
    MENU_PROGRAM_START,
    MENU_READ_BACKUP,
    MENU_READ_SUB,
    MENU_CHIP_ERASE,
    MENU_FIRMWARE_MGMT,
    MENU_FIRMWARE_SUB,
    MENU_SETTINGS,
    MENU_SETTINGS_SUB,
    MENU_CHIP_SELECT,
    MENU_FIRMWARE_SELECT,
    MENU_BACKUP_LIST,
    MENU_PROGRAM_PROGRESS,
    MENU_CONFIRM,
    MENU_INFO,
    MENU_MAX
} menu_id_t;

typedef struct {
    char name[32];
    uint16_t device_id;
    uint16_t id_mask;
    chip_type_t type;
    uint32_t flash_addr;
    uint32_t flash_size;
    uint32_t ram_size;
    uint32_t page_size;
    uint32_t sector_size;
    bool valid;
} chip_info_t;

typedef struct {
    char filename[MAX_FILENAME_LEN];
    char path[MAX_PATH_LEN];
    file_type_t type;
    uint32_t size;
    uint32_t timestamp;
    bool valid;
} file_info_t;

typedef struct {
    uint32_t start_addr;
    uint32_t size;
    uint8_t *data;
} memory_segment_t;

typedef struct {
    file_type_t type;
    uint32_t total_size;
    uint32_t base_addr;
    int segment_count;
    memory_segment_t segments[16];
} firmware_info_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    program_mode_t program_mode;
    interface_mode_t interface_mode;
    swd_speed_t swd_speed;
    bool stability_check_enabled;
    bool allow_unknown_chip;
    bool verify_after_program;
    bool backup_before_program;
    bool auto_erase;
    int default_chip_index;
    char default_firmware[MAX_FILENAME_LEN];
    uint8_t brightness;
    bool screen_auto_off;
    uint16_t screen_timeout;
    uint32_t crc32;
} system_config_t;

typedef struct {
    system_config_t config;
    chip_info_t detected_chip;
    chip_info_t selected_chip;
    bool chip_connected;
    bool chip_detected;
    file_info_t file_list[MAX_FILES];
    int file_count;
    prog_state_t prog_state;
    uint32_t prog_progress;
    uint32_t prog_total;
    uint32_t prog_speed;
    char prog_message[64];
    int error_code;
    bool auto_program_locked;
    bool usb_connected;
    uint32_t start_time;
    uint8_t *read_buffer;
    uint32_t read_buffer_size;
} programmer_state_t;

typedef struct {
    menu_id_t current_menu;
    menu_id_t previous_menu;
    int selected_item;
    int scroll_offset;
    bool need_refresh;
    char info_title[32];
    char info_message[256];
    bool confirm_result;
    void *user_data;
} menu_state_t;

//=============================================================================
// 全局变量
//=============================================================================

static programmer_state_t g_state = {0};
static menu_state_t g_menu = {0};
static chip_info_t g_chip_db[MAX_CHIP_DB] = {0};
static int g_chip_db_count = 0;
static uint8_t g_oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8] = {0};
static uint8_t g_stream_buffer[STREAM_BUFFER_SIZE] __attribute__((aligned(4)));
static firmware_info_t g_firmware_info = {0};

// 前置声明
extern bool font_get_chinese(uint16_t unicode, uint8_t data[24]);
extern void menu_system_init(void);
extern void menu_system_process(menu_state_t *menu, key_event_t key, programmer_state_t *state);
extern void menu_system_draw(menu_state_t *menu, programmer_state_t *state);

//=============================================================================
// 6x8 ASCII字体
//=============================================================================

static const uint8_t font_6x8[][6] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00}, {0x23, 0x13, 0x08, 0x64, 0x62, 0x00},
    {0x36, 0x49, 0x56, 0x20, 0x50, 0x00}, {0x00, 0x08, 0x07, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00},
    {0x2A, 0x1C, 0x7F, 0x1C, 0x2A, 0x00}, {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00},
    {0x00, 0x80, 0x70, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08, 0x00},
    {0x00, 0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02, 0x00},
    {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00}, {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00},
    {0x72, 0x49, 0x49, 0x49, 0x46, 0x00}, {0x21, 0x41, 0x49, 0x4D, 0x33, 0x00},
    {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00}, {0x27, 0x45, 0x45, 0x45, 0x39, 0x00},
    {0x3C, 0x4A, 0x49, 0x49, 0x31, 0x00}, {0x41, 0x21, 0x11, 0x09, 0x07, 0x00},
    {0x36, 0x49, 0x49, 0x49, 0x36, 0x00}, {0x46, 0x49, 0x49, 0x29, 0x1E, 0x00},
    {0x00, 0x00, 0x14, 0x00, 0x00, 0x00}, {0x00, 0x40, 0x34, 0x00, 0x00, 0x00},
    {0x00, 0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14, 0x00},
    {0x00, 0x41, 0x22, 0x14, 0x08, 0x00}, {0x02, 0x01, 0x59, 0x09, 0x06, 0x00},
    {0x3E, 0x41, 0x5D, 0x59, 0x4E, 0x00}, {0x7C, 0x12, 0x11, 0x12, 0x7C, 0x00},
    {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00}, {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00},
    {0x7F, 0x41, 0x41, 0x41, 0x3E, 0x00}, {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00},
    {0x7F, 0x09, 0x09, 0x09, 0x01, 0x00}, {0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00},
    {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00}, {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00}, {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00},
    {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00}, {0x7F, 0x02, 0x1C, 0x02, 0x7F, 0x00},
    {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00}, {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00},
    {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00}, {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00},
    {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00}, {0x26, 0x49, 0x49, 0x49, 0x32, 0x00},
    {0x03, 0x01, 0x7F, 0x01, 0x03, 0x00}, {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00},
    {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00}, {0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00},
    {0x63, 0x14, 0x08, 0x14, 0x63, 0x00}, {0x03, 0x04, 0x78, 0x04, 0x03, 0x00},
    {0x61, 0x59, 0x49, 0x4D, 0x43, 0x00}, {0x00, 0x7F, 0x41, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20, 0x00}, {0x00, 0x41, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04, 0x00}, {0x40, 0x40, 0x40, 0x40, 0x40, 0x00},
    {0x00, 0x03, 0x07, 0x08, 0x00, 0x00}, {0x20, 0x54, 0x54, 0x78, 0x40, 0x00},
    {0x7F, 0x28, 0x44, 0x44, 0x38, 0x00}, {0x38, 0x44, 0x44, 0x44, 0x28, 0x00},
    {0x38, 0x44, 0x44, 0x28, 0x7F, 0x00}, {0x38, 0x54, 0x54, 0x54, 0x18, 0x00},
    {0x00, 0x08, 0x7E, 0x09, 0x02, 0x00}, {0x18, 0xA4, 0xA4, 0x9C, 0x78, 0x00},
    {0x7F, 0x08, 0x04, 0x04, 0x78, 0x00}, {0x00, 0x44, 0x7D, 0x40, 0x00, 0x00},
    {0x20, 0x40, 0x40, 0x3D, 0x00, 0x00}, {0x7F, 0x10, 0x28, 0x44, 0x00, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00, 0x00}, {0x7C, 0x04, 0x78, 0x04, 0x78, 0x00},
    {0x7C, 0x08, 0x04, 0x04, 0x78, 0x00}, {0x38, 0x44, 0x44, 0x44, 0x38, 0x00},
    {0xFC, 0x18, 0x24, 0x24, 0x18, 0x00}, {0x18, 0x24, 0x24, 0x18, 0xFC, 0x00},
    {0x7C, 0x08, 0x04, 0x04, 0x08, 0x00}, {0x48, 0x54, 0x54, 0x54, 0x24, 0x00},
    {0x04, 0x04, 0x3F, 0x44, 0x24, 0x00}, {0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00},
    {0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00}, {0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00},
    {0x44, 0x28, 0x10, 0x28, 0x44, 0x00}, {0x4C, 0x90, 0x90, 0x90, 0x7C, 0x00},
    {0x44, 0x64, 0x54, 0x4C, 0x44, 0x00}, {0x00, 0x08, 0x36, 0x41, 0x00, 0x00},
    {0x00, 0x00, 0x77, 0x00, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00, 0x00},
    {0x02, 0x01, 0x02, 0x04, 0x02, 0x00},
};

//=============================================================================
// UTF-8 和字库
//=============================================================================

static uint16_t utf8_to_unicode(const char *utf8, int *bytes) {
    uint8_t c1 = utf8[0];
    
    if ((c1 & 0x80) == 0) {
        *bytes = 1;
        return c1;
    } else if ((c1 & 0xE0) == 0xC0) {
        *bytes = 2;
        return ((c1 & 0x1F) << 6) | (utf8[1] & 0x3F);
    } else if ((c1 & 0xF0) == 0xE0) {
        *bytes = 3;
        return ((c1 & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
    }
    
    *bytes = 1;
    return 0;
}

//=============================================================================
// OLED驱动
//=============================================================================

static void oled_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

static void oled_write_data(uint8_t data) {
    uint8_t buf[2] = {0x40, data};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

static bool oled_init(void) {
    sleep_ms(100);
    oled_write_cmd(0xAE); oled_write_cmd(0x00); oled_write_cmd(0x10);
    oled_write_cmd(0x40); oled_write_cmd(0xB0); oled_write_cmd(0x81);
    oled_write_cmd(0xCF); oled_write_cmd(0xA1); oled_write_cmd(0xA6);
    oled_write_cmd(0xA8); oled_write_cmd(0x3F); oled_write_cmd(0xC8);
    oled_write_cmd(0xD3); oled_write_cmd(0x00); oled_write_cmd(0xD5);
    oled_write_cmd(0x80); oled_write_cmd(0xD9); oled_write_cmd(0xF1);
    oled_write_cmd(0xDA); oled_write_cmd(0x12); oled_write_cmd(0xDB);
    oled_write_cmd(0x40); oled_write_cmd(0x8D); oled_write_cmd(0x14);
    oled_write_cmd(0xAF);
    return true;
}

static void oled_clear(void) {
    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));
}

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

static void oled_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    int index = page * OLED_WIDTH + x;
    if (on) {
        g_oled_buffer[index] |= (1 << bit);
    } else {
        g_oled_buffer[index] &= ~(1 << bit);
    }
}

static void oled_draw_hline(int x, int y, int w) {
    for (int i = 0; i < w; i++) oled_draw_pixel(x + i, y, true);
}

static void oled_draw_vline(int x, int y, int h) {
    for (int i = 0; i < h; i++) oled_draw_pixel(x, y + i, true);
}

static void oled_draw_rect(int x, int y, int w, int h, bool fill) {
    if (fill) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                oled_draw_pixel(x + j, y + i, true);
            }
        }
    } else {
        oled_draw_hline(x, y, w);
        oled_draw_hline(x, y + h - 1, w);
        oled_draw_vline(x, y, h);
        oled_draw_vline(x + w - 1, y, h);
    }
}

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

static int oled_show_chinese(int x, int y, uint16_t unicode) {
    uint8_t font_data[24];
    if (font_get_chinese(unicode, font_data)) {
        for (int row = 0; row < 12; row++) {
            uint16_t line = (font_data[row * 2] << 8) | font_data[row * 2 + 1];
            for (int col = 0; col < 12; col++) {
                if (line & (0x8000 >> col)) {
                    oled_draw_pixel(x + col, y + row, true);
                }
            }
        }
        return 12;
    }
    return 0;
}

void oled_show_string(int x, int y, const char *str) {
    int pos_x = x;
    const char *p = str;
    while (*p && pos_x < OLED_WIDTH) {
        if ((*p & 0x80) == 0) {
            oled_show_char(pos_x, y, *p);
            pos_x += 6;
            p++;
        } else {
            int bytes;
            uint16_t unicode = utf8_to_unicode(p, &bytes);
            int width = oled_show_chinese(pos_x, y, unicode);
            if (width > 0) {
                pos_x += width;
            } else {
                pos_x += 12;
            }
            p += bytes;
        }
    }
}

void oled_show_number(int x, int y, uint32_t num) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)num);
    oled_show_string(x, y, buf);
}

void oled_show_progress(int x, int y, int w, int h, uint32_t value, uint32_t total) {
    oled_draw_rect(x, y, w, h, false);
    if (total > 0) {
        int progress_w = (int)((uint64_t)value * (w - 2) / total);
        if (progress_w > 0 && progress_w <= w - 2) {
            oled_draw_rect(x + 1, y + 1, progress_w, h - 2, true);
        }
    }
}

void oled_show_percent(int x, int y, uint32_t value, uint32_t total) {
    char buf[8];
    if (total > 0) {
        snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(value * 100 / total));
    } else {
        snprintf(buf, sizeof(buf), "0%%");
    }
    oled_show_string(x, y, buf);
}

void oled_show_speed(int x, int y, uint32_t bytes_per_sec) {
    char buf[16];
    if (bytes_per_sec >= 1024) {
        snprintf(buf, sizeof(buf), "%luK/s", (unsigned long)(bytes_per_sec / 1024));
    } else {
        snprintf(buf, sizeof(buf), "%luB/s", (unsigned long)bytes_per_sec);
    }
    oled_show_string(x, y, buf);
}

//=============================================================================
// 按键
//=============================================================================

static uint32_t g_key_last_time[4] = {0};
static bool g_key_last_state[4] = {false};
static uint32_t g_key_press_time[4] = {0};

static void keys_init(void) {
    memset(g_key_last_time, 0, sizeof(g_key_last_time));
    memset(g_key_last_state, 0, sizeof(g_key_last_state));
    memset(g_key_press_time, 0, sizeof(g_key_press_time));
}

static bool key_read(int pin) {
    return !gpio_get(pin);
}

key_event_t keys_scan(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    const int pins[] = {PIN_KEY1, PIN_KEY2, PIN_KEY3, PIN_KEY4};
    const key_event_t events[] = {KEY_UP, KEY_DOWN, KEY_OK, KEY_BACK};
    
    for (int i = 0; i < 4; i++) {
        bool current = key_read(pins[i]);
        if (current && !g_key_last_state[i]) {
            g_key_press_time[i] = now;
            g_key_last_state[i] = true;
            g_key_last_time[i] = now;
            return events[i];
        } else if (current && g_key_last_state[i]) {
            uint32_t hold_time = now - g_key_press_time[i];
            if (hold_time > KEY_REPEAT_MS) {
                uint32_t repeat_interval = (hold_time > KEY_REPEAT_MS * 2) ? 
                                          KEY_REPEAT_FAST_MS : KEY_REPEAT_MS;
                if (now - g_key_last_time[i] > repeat_interval) {
                    g_key_last_time[i] = now;
                    return events[i];
                }
            }
        } else if (!current && g_key_last_state[i]) {
            g_key_last_state[i] = false;
        }
    }
    return KEY_NONE;
}

//=============================================================================
// CRC32
//=============================================================================

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void crc32_init(void) {
    if (crc32_table_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

static uint32_t crc32_calculate(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
    }
    return ~crc;
}

//=============================================================================
// 配置管理
//=============================================================================

#define CONFIG_MAGIC 0x53544D50
#define CONFIG_VERSION 1

static void config_set_default(system_config_t *config) {
    memset(config, 0, sizeof(system_config_t));
    config->magic = CONFIG_MAGIC;
    config->version = CONFIG_VERSION;
    config->program_mode = PROGRAM_MODE_AUTO;
    config->interface_mode = INTERFACE_AUTO;
    config->swd_speed = SWD_SPEED_MEDIUM;
    config->stability_check_enabled = true;
    config->allow_unknown_chip = false;
    config->verify_after_program = true;
    config->backup_before_program = false;
    config->auto_erase = true;
    config->default_chip_index = -1;
    strcpy(config->default_firmware, "");
    config->brightness = 200;
    config->screen_auto_off = false;
    config->screen_timeout = 60;
}

static bool config_load(system_config_t *config) {
    const uint8_t *flash_config = (const uint8_t *)(XIP_BASE + CONFIG_OFFSET);
    memcpy(config, flash_config, sizeof(system_config_t));
    
    if (config->magic != CONFIG_MAGIC) {
        config_set_default(config);
        return false;
    }
    
    uint32_t saved_crc = config->crc32;
    config->crc32 = 0;
    uint32_t calc_crc = crc32_calculate((uint8_t *)config, sizeof(system_config_t));
    config->crc32 = saved_crc;
    
    if (calc_crc != saved_crc) {
        config_set_default(config);
        return false;
    }
    return true;
}

static bool config_save(system_config_t *config) {
    config->crc32 = 0;
    config->crc32 = crc32_calculate((uint8_t *)config, sizeof(system_config_t));
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_OFFSET, CONFIG_SIZE);
    flash_range_program(CONFIG_OFFSET, (uint8_t *)config, sizeof(system_config_t));
    restore_interrupts(ints);
    return true;
}
