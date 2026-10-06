// stm_programmer.h - 公共头文件
#ifndef STM_PROGRAMMER_H
#define STM_PROGRAMMER_H

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
#include "hardware/clocks.h"

//=============================================================================
// 配置定义
//=============================================================================
#define PIN_OLED_SCL    29
#define PIN_OLED_SDA    28
#define PIN_KEY1        27
#define PIN_KEY2        26
#define PIN_KEY3        15
#define PIN_KEY4        14
#define PIN_SWIM_SWDIO  2
#define PIN_SWCLK       3
#define PIN_NRST        4

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDR       0x3C

#define CONFIG_OFFSET           0x100000
#define CONFIG_SIZE             (4 * 1024)
#define FONT_OFFSET             0x101000
#define FONT_SIZE               (192 * 1024)
#define FILESYSTEM_OFFSET       0x131000
#define FILESYSTEM_SIZE         (828 * 1024)

#define STREAM_BUFFER_SIZE      (4 * 1024)
#define STM8_WRITE_BLOCK        128
#define STM32_WRITE_BLOCK       256
#define MAX_FILENAME_LEN        64
#define MAX_PATH_LEN            128
#define MAX_FILES               64
#define MAX_CHIP_DB             128

#define MENU_LINE_HEIGHT    11
#define MENU_START_Y        15
#define MENU_ITEMS_PER_PAGE 4

#define SWIM_CSR        0x7F80
#define SWIM_CSR2       0x7F81

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
// 全局变量声明
//=============================================================================
extern programmer_state_t g_state;
extern menu_state_t g_menu;
extern chip_info_t g_chip_db[MAX_CHIP_DB];
extern int g_chip_db_count;
extern uint8_t g_oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];
extern uint8_t g_stream_buffer[STREAM_BUFFER_SIZE];

//=============================================================================
// 函数声明
//=============================================================================
extern bool font_get_chinese(uint16_t unicode, uint8_t data[24]);
extern void oled_clear(void);
extern void oled_refresh(void);
extern void oled_show_string(int x, int y, const char *str);
extern void oled_show_number(int x, int y, uint32_t num);
extern void oled_show_progress(int x, int y, int w, int h, uint32_t value, uint32_t total);
extern void oled_show_percent(int x, int y, uint32_t value, uint32_t total);
extern void oled_show_speed(int x, int y, uint32_t bytes_per_sec);
extern void oled_draw_hline(int x, int y, int w);
extern void oled_draw_vline(int x, int y, int h);
extern void oled_draw_rect(int x, int y, int w, int h, bool fill);
extern void oled_draw_pixel(int x, int y, bool on);

#endif // STM_PROGRAMMER_H