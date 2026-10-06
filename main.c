// main.c - 主程序
#include "stm_programmer.h"
#include "oled_ui.h"  // 新增

//=============================================================================
// 全局变量定义
//=============================================================================
programmer_state_t g_state = {0};
menu_state_t g_menu = {0};
chip_info_t g_chip_db[MAX_CHIP_DB] = {0};
int g_chip_db_count = 0;
uint8_t g_oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8] = {0};
uint8_t g_stream_buffer[STREAM_BUFFER_SIZE] __attribute__((aligned(4)));
firmware_info_t g_firmware_info = {0};

// 前置声明
extern bool font_get_chinese(uint16_t unicode, uint8_t data[24]);
extern void chip_db_init(void);
extern void menu_system_init(void);
extern void menu_system_process(menu_state_t *menu, key_event_t key, programmer_state_t *state);

//=============================================================================
// 6x8 ASCII字体 (保留兼容性)
//=============================================================================
static const uint8_t font_6x8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00,0x00}, {0x14,0x7F,0x14,0x7F,0x14,0x00},
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, {0x23,0x13,0x08,0x64,0x62,0x00},
    {0x36,0x49,0x56,0x20,0x50,0x00}, {0x00,0x08,0x07,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00,0x00}, {0x00,0x41,0x22,0x1C,0x00,0x00},
    {0x2A,0x1C,0x7F,0x1C,0x2A,0x00}, {0x08,0x08,0x3E,0x08,0x08,0x00},
    {0x00,0x80,0x70,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08,0x00},
    {0x00,0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02,0x00},
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, {0x00,0x42,0x7F,0x40,0x00,0x00},
    {0x72,0x49,0x49,0x49,0x46,0x00}, {0x21,0x41,0x49,0x4D,0x33,0x00},
    {0x18,0x14,0x12,0x7F,0x10,0x00}, {0x27,0x45,0x45,0x45,0x39,0x00},
    {0x3C,0x4A,0x49,0x49,0x31,0x00}, {0x41,0x21,0x11,0x09,0x07,0x00},
    {0x36,0x49,0x49,0x49,0x36,0x00}, {0x46,0x49,0x49,0x29,0x1E,0x00},
    {0x00,0x00,0x14,0x00,0x00,0x00}, {0x00,0x40,0x34,0x00,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14,0x00},
    {0x00,0x41,0x22,0x14,0x08,0x00}, {0x02,0x01,0x59,0x09,0x06,0x00},
    {0x3E,0x41,0x5D,0x59,0x4E,0x00}, {0x7C,0x12,0x11,0x12,0x7C,0x00},
    {0x7F,0x49,0x49,0x49,0x36,0x00}, {0x3E,0x41,0x41,0x41,0x22,0x00},
    {0x7F,0x41,0x41,0x41,0x3E,0x00}, {0x7F,0x49,0x49,0x49,0x41,0x00},
    {0x7F,0x09,0x09,0x09,0x01,0x00}, {0x3E,0x41,0x49,0x49,0x7A,0x00},
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, {0x00,0x41,0x7F,0x41,0x00,0x00},
    {0x20,0x40,0x41,0x3F,0x01,0x00}, {0x7F,0x08,0x14,0x22,0x41,0x00},
    {0x7F,0x40,0x40,0x40,0x40,0x00}, {0x7F,0x02,0x1C,0x02,0x7F,0x00},
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, {0x3E,0x41,0x41,0x41,0x3E,0x00},
    {0x7F,0x09,0x09,0x09,0x06,0x00}, {0x3E,0x41,0x51,0x21,0x5E,0x00},
    {0x7F,0x09,0x19,0x29,0x46,0x00}, {0x26,0x49,0x49,0x49,0x32,0x00},
    {0x03,0x01,0x7F,0x01,0x03,0x00}, {0x3F,0x40,0x40,0x40,0x3F,0x00},
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, {0x3F,0x40,0x38,0x40,0x3F,0x00},
    {0x63,0x14,0x08,0x14,0x63,0x00}, {0x03,0x04,0x78,0x04,0x03,0x00},
    {0x61,0x59,0x49,0x4D,0x43,0x00}, {0x00,0x7F,0x41,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20,0x00}, {0x00,0x41,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04,0x00}, {0x40,0x40,0x40,0x40,0x40,0x00},
    {0x00,0x03,0x07,0x08,0x00,0x00}, {0x20,0x54,0x54,0x78,0x40,0x00},
    {0x7F,0x28,0x44,0x44,0x38,0x00}, {0x38,0x44,0x44,0x44,0x28,0x00},
    {0x38,0x44,0x44,0x28,0x7F,0x00}, {0x38,0x54,0x54,0x54,0x18,0x00},
    {0x00,0x08,0x7E,0x09,0x02,0x00}, {0x18,0xA4,0xA4,0x9C,0x78,0x00},
    {0x7F,0x08,0x04,0x04,0x78,0x00}, {0x00,0x44,0x7D,0x40,0x00,0x00},
    {0x20,0x40,0x40,0x3D,0x00,0x00}, {0x7F,0x10,0x28,0x44,0x00,0x00},
    {0x00,0x41,0x7F,0x40,0x00,0x00}, {0x7C,0x04,0x78,0x04,0x78,0x00},
    {0x7C,0x08,0x04,0x04,0x78,0x00}, {0x38,0x44,0x44,0x44,0x38,0x00},
    {0xFC,0x18,0x24,0x24,0x18,0x00}, {0x18,0x24,0x24,0x18,0xFC,0x00},
    {0x7C,0x08,0x04,0x04,0x08,0x00}, {0x48,0x54,0x54,0x54,0x24,0x00},
    {0x04,0x04,0x3F,0x44,0x24,0x00}, {0x3C,0x40,0x40,0x20,0x7C,0x00},
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, {0x3C,0x40,0x30,0x40,0x3C,0x00},
    {0x44,0x28,0x10,0x28,0x44,0x00}, {0x4C,0x90,0x90,0x90,0x7C,0x00},
    {0x44,0x64,0x54,0x4C,0x44,0x00}, {0x00,0x08,0x36,0x41,0x00,0x00},
    {0x00,0x00,0x77,0x00,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00,0x00},
    {0x02,0x01,0x02,0x04,0x02,0x00},
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
// OLED驱动 (保持兼容性，但内部调用新系统)
//=============================================================================
static void oled_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

static void oled_write_data(uint8_t data) {
    uint8_t buf[2] = {0x40, data};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

bool oled_init(void) {
    return oled_ui_init();
}

void oled_clear(void) {
    oled_ui_clear();
}

void oled_refresh(void) {
    oled_ui_refresh();
}

void oled_draw_pixel(int x, int y, bool on) {
    // 保持兼容性，内部调用新系统
    extern uint8_t oled_buffer[];
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    int index = page * OLED_WIDTH + x;
    if (on) {
        oled_buffer[index] |= (1 << bit);
    } else {
        oled_buffer[index] &= ~(1 << bit);
    }
}

void oled_draw_hline(int x, int y, int w) {
    for (int i = 0; i < w; i++) oled_draw_pixel(x + i, y, true);
}

void oled_draw_vline(int x, int y, int h) {
    for (int i = 0; i < h; i++) oled_draw_pixel(x, y + i, true);
}

void oled_draw_rect(int x, int y, int w, int h, bool fill) {
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

void oled_show_string(int x, int y, const char *str) {
    // 保持兼容性，内部调用新系统
    extern uint8_t oled_buffer[];
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
            if (bytes > 1 && unicode > 0) {
                uint8_t font_data[24];
                if (font_get_chinese(unicode, font_data)) {
                    for (int row = 0; row < 12; row++) {
                        uint8_t byte1 = font_data[row * 2];
                        uint8_t byte2 = font_data[row * 2 + 1];
                        for (int bit = 0; bit < 8; bit++) {
                            if (byte1 & (0x80 >> bit)) {
                                oled_draw_pixel(pos_x + bit, y + row, true);
                            }
                        }
                        for (int bit = 0; bit < 4; bit++) {
                            if (byte2 & (0x80 >> bit)) {
                                oled_draw_pixel(pos_x + 8 + bit, y + row, true);
                            }
                        }
                    }
                    pos_x += 12;
                }
                p += bytes;
            } else {
                p++;
            }
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
        int progress = (int)((uint64_t)value * (w - 2) / total);
        if (progress > 0 && progress <= w - 2) {
            oled_draw_rect(x + 1, y + 1, progress, h - 2, true);
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
// 按键处理 (使用新的按键命名)
//=============================================================================
static uint32_t g_key_last_time[4] = {0};
static bool g_key_last_state[4] = {false};
static uint32_t g_key_press_time[4] = {0};

void keys_init(void) {
    memset(g_key_last_time, 0, sizeof(g_key_last_time));
    memset(g_key_last_state, 0, sizeof(g_key_last_state));
    memset(g_key_press_time, 0, sizeof(g_key_press_time));
}

static bool key_read(int pin) {
    return !gpio_get(pin);
}

key_event_t keys_scan(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    const int pins[] = {PIN_KEY_BACK, PIN_KEY_OK, PIN_KEY_DOWN, PIN_KEY_UP};
    const key_event_t events[] = { KEY_BACK, KEY_OK, KEY_DOWN, KEY_UP};
    
    for (int i = 0; i < 4; i++) {
        bool current = key_read(pins[i]);
        if (current && !g_key_last_state[i]) {
            g_key_press_time[i] = now;
            g_key_last_state[i] = true;
            g_key_last_time[i] = now;
            return events[i];
        } else if (current && g_key_last_state[i]) {
            uint32_t hold_time = now - g_key_press_time[i];
            if (hold_time > 500) {
                uint32_t repeat_interval = (hold_time > 1000) ? 100 : 500;
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

void crc32_init(void) {
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

uint32_t crc32_calculate(const uint8_t *data, uint32_t len) {
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

void config_set_default(system_config_t *config) {
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

bool config_load(system_config_t *config) {
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

bool config_save(system_config_t *config) {
    config->crc32 = 0;
    config->crc32 = crc32_calculate((uint8_t *)config, sizeof(system_config_t));
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_OFFSET, CONFIG_SIZE);
    flash_range_program(CONFIG_OFFSET, (uint8_t *)config, sizeof(system_config_t));
    restore_interrupts(ints);
    return true;
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

//=============================================================================
// 系统初始化
//=============================================================================

static bool system_init(void) {
    // 使用新的显示系统
    if (!oled_ui_init()) {
        return false;
    }
    
    // 使用新的启动画面
    ui_show_startup();
    
    crc32_init();
    keys_init();
    chip_db_init();
    
    if (!config_load(&g_state.config)) {
        config_set_default(&g_state.config);
        config_save(&g_state.config);
    }
    
    if (!fs_init()) {
        ui_show_message("错误", "文件系统初始化失败");
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