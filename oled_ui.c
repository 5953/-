#include "oled_ui.h"
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// 全局缓冲区
uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8] = {0};

// I2C接口
static void oled_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

static void oled_write_data(uint8_t data) {
    uint8_t buf[2] = {0x40, data};
    i2c_write_blocking(i2c0, OLED_ADDR, buf, 2, false);
}

// OLED初始化
bool oled_ui_init(void) {
    sleep_ms(50);
    
    // SSD1315完整初始化序列
    uint8_t init_seq[] = {
        0xAE,        // 关闭显示
        0xD5, 0x80,  // 设置显示时钟分频因子
        0xA8, 0x3F,  // 设置驱动路数(1/64 duty)
        0xD3, 0x00,  // 设置显示偏移
        0x40,        // 设置起始行
        0x8D, 0x14,  // 电荷泵设置
        0x20, 0x00,  // 设置内存地址模式(水平)
        0xA1,        // 段重定向(0->127)
        0xC8,        // 设置COM扫描方向(从下到上)
        0xDA, 0x12,  // 设置COM硬件引脚配置
        0x81, 0xCF,  // 对比度设置
        0xD9, 0xF1,  // 设置预充电周期
        0xDB, 0x40,  // 设置VCOMH电压倍率
        0xA4,        // 全局显示开启(正常显示)
        0xA6,        // 设置显示方式(正常/反显)
        0xAF         // 开启显示
    };
    
    for (int i = 0; i < sizeof(init_seq); i++) {
        oled_write_cmd(init_seq[i]);
    }
    
    oled_ui_clear();
    return true;
}

// 清屏
void oled_ui_clear(void) {
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

// 刷新显示
void oled_ui_refresh(void) {
    for (int page = 0; page < 8; page++) {
        oled_write_cmd(0xB0 + page);        // 页地址
        oled_write_cmd(0x00);              // 列地址低
        oled_write_cmd(0x10);              // 列地址高
        
        for (int col = 0; col < OLED_WIDTH; col++) {
            oled_write_data(oled_buffer[page * OLED_WIDTH + col]);
        }
    }
}

// 像素操作
void oled_draw_pixel(int x, int y, bool on) {
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

// ASCII字体 (6x8)
#include "font_ascii.h"

// UTF-8转Unicode
static uint16_t utf8_to_unicode(const char *str, int *bytes) {
    uint8_t c1 = (uint8_t)str[0];
    
    if (c1 < 0x80) {
        *bytes = 1;
        return c1;
    } else if ((c1 & 0xE0) == 0xC0) {
        *bytes = 2;
        return ((c1 & 0x1F) << 6) | (str[1] & 0x3F);
    } else if ((c1 & 0xF0) == 0xE0) {
        *bytes = 3;
        return ((c1 & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
    }
    
    *bytes = 1;
    return 0;
}

// 绘制字符
void ui_draw_char(int x, int y, char c) {
    if (c < 0x20 || c > 0x7E || x >= OLED_WIDTH - FONT_ASCII_WIDTH) return;
    
    int index = c - 0x20;
    for (int i = 0; i < FONT_ASCII_WIDTH; i++) {
        uint8_t line = font_ascii[index][i];
        for (int j = 0; j < FONT_ASCII_HEIGHT; j++) {
            if (line & (1 << j)) {
                oled_draw_pixel(x + i, y + j, true);
            }
        }
    }
}

// 绘制文本
void ui_draw_text(int x, int y, const char *text) {
    if (!text || x >= OLED_WIDTH) return;
    
    int pos_x = x;
    const char *p = text;
    
    while (*p && pos_x < OLED_WIDTH) {
        if ((*p & 0x80) == 0) {
            // ASCII字符
            ui_draw_char(pos_x, y, *p);
            pos_x += FONT_ASCII_WIDTH;
            p++;
        } else {
            // UTF-8汉字
            int bytes;
            uint16_t unicode = utf8_to_unicode(p, &bytes);
            
            if (bytes > 1 && unicode > 0) {
                uint8_t font_data[24];
                if (font_get_chinese(unicode, font_data)) {
                    for (int row = 0; row < FONT_CHINESE_HEIGHT; row++) {
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
                    pos_x += FONT_CHINESE_WIDTH;
                } else {
                    // 汉字不存在，显示方框
                    ui_draw_rect(pos_x, y, FONT_CHINESE_WIDTH, FONT_CHINESE_HEIGHT, false);
                    pos_x += FONT_CHINESE_WIDTH;
                }
                p += bytes;
            } else {
                p++;
            }
        }
    }
}

// 绘制数字
void ui_draw_number(int x, int y, uint32_t num) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)num);
    ui_draw_text(x, y, buf);
}

// 绘制直线
void ui_draw_line(int x1, int y1, int x2, int y2) {
    if (x1 == x2) {
        int start = y1 < y2 ? y1 : y2;
        int end = y1 < y2 ? y2 : y1;
        for (int y = start; y <= end; y++) {
            oled_draw_pixel(x1, y, true);
        }
    } else if (y1 == y2) {
        int start = x1 < x2 ? x1 : x2;
        int end = x1 < x2 ? x2 : x1;
        for (int x = start; x <= end; x++) {
            oled_draw_pixel(x, y1, true);
        }
    }
}

// 绘制矩形
void ui_draw_rect(int x, int y, int w, int h, bool fill) {
    if (fill) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                oled_draw_pixel(x + j, y + i, true);
            }
        }
    } else {
        ui_draw_line(x, y, x + w - 1, y);
        ui_draw_line(x, y + h - 1, x + w - 1, y + h - 1);
        ui_draw_line(x, y, x, y + h - 1);
        ui_draw_line(x + w - 1, y, x + w - 1, y + h - 1);
    }
}

// 绘制进度条
void ui_draw_progress(int x, int y, int w, int h, uint32_t value, uint32_t total) {
    ui_draw_rect(x, y, w, h, false);
    if (total > 0) {
        int progress = (int)((uint64_t)value * (w - 2) / total);
        if (progress > 0 && progress <= w - 2) {
            ui_draw_rect(x + 1, y + 1, progress, h - 2, true);
        }
    }
}

// 显示百分比
void ui_draw_percent(int x, int y, uint32_t value, uint32_t total) {
    char buf[8];
    if (total > 0) {
        snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)(value * 100 / total));
    } else {
        snprintf(buf, sizeof(buf), "0%%");
    }
    ui_draw_text(x, y, buf);
}

// 菜单初始化
void ui_menu_init(menu_context_t *menu, int total_items) {
    menu->selected = 0;
    menu->scroll = 0;
    menu->total_items = total_items;
    menu->items_per_page = 4;  // 128x64显示4行，每行14px
    menu->start_y = 15;
    menu->end_y = OLED_HEIGHT - 12;  // 留出底部空间
}

// 菜单导航
void ui_menu_navigate(menu_context_t *menu, key_event_t key, int *selected_item) {
    switch (key) {
        case KEY_UP:
            if (menu->selected > 0) {
                menu->selected--;
                if (menu->selected < menu->scroll) {
                    menu->scroll = menu->selected;
                }
            } else {
                menu->selected = menu->total_items - 1;
                menu->scroll = menu->total_items - menu->items_per_page;
                if (menu->scroll < 0) menu->scroll = 0;
            }
            break;
            
        case KEY_DOWN:
            if (menu->selected < menu->total_items - 1) {
                menu->selected++;
                if (menu->selected >= menu->scroll + menu->items_per_page) {
                    menu->scroll = menu->selected - menu->items_per_page + 1;
                }
            } else {
                menu->selected = 0;
                menu->scroll = 0;
            }
            break;
            
        case KEY_OK:
            *selected_item = menu->selected;
            break;
            
        default:
            break;
    }
}

// 绘制菜单
void ui_menu_draw(menu_context_t *menu, const char *title, const char **items, int item_count) {
    oled_ui_clear();
    
    // 标题
    ui_draw_text(5, 2, title);
    ui_draw_line(0, 12, OLED_WIDTH - 1, 12);
    
    // 菜单项
    int visible_items = menu->items_per_page;
    int end_item = (menu->scroll + visible_items < item_count) ? 
                   (menu->scroll + visible_items) : item_count;
    
    for (int i = menu->scroll; i < end_item; i++) {
        int y = menu->start_y + (i - menu->scroll) * MENU_LINE_HEIGHT;
        
        if (i == menu->selected) {
            ui_draw_char(2, y, '▶');
        }
        
        ui_draw_text(12, y, items[i]);
    }
    
    // 底部提示
    ui_draw_text(OLED_WIDTH - 40, OLED_HEIGHT - 10, "↑↓选择 OK确认");
    
    // 滚动条
    if (item_count > visible_items) {
        int bar_height = (menu->end_y - menu->start_y) * visible_items / item_count;
        int bar_pos = (menu->end_y - menu->start_y - bar_height) * menu->scroll / 
                     (item_count - visible_items);
        ui_draw_rect(OLED_WIDTH - 3, menu->start_y + bar_pos, 2, bar_height, true);
    }
    
    oled_ui_refresh();
}

// 启动画面
void ui_show_startup(void) {
    oled_ui_clear();
    ui_draw_text(OLED_WIDTH/2 - 20, OLED_HEIGHT/2 - 8, "STM烧录器");
    ui_draw_text(OLED_WIDTH/2 - 20, OLED_HEIGHT/2 + 8, "初始化中...");
    oled_ui_refresh();
}

// 主菜单
void ui_show_main_menu(void) {
    const char *main_menu_items[] = {
        "开始烧录",
        "读取备份", 
        "擦除芯片",
        "固件管理",
        "系统设置"
    };
    
    menu_context_t menu;
    ui_menu_init(&menu, 5);
    ui_menu_draw(&menu, "STM烧录器", main_menu_items, 5);
}

// 消息显示
void ui_show_message(const char *title, const char *message) {
    oled_ui_clear();
    
    ui_draw_text(5, 2, title);
    ui_draw_line(0, 12, OLED_WIDTH - 1, 12);
    
    int y = 20;
    char *line = (char *)message;
    while (*line && y < OLED_HEIGHT - 8) {
        char buf[22];
        int i = 0;
        
        while (*line && *line != '\n' && i < 21) {
            buf[i++] = *line++;
        }
        buf[i] = 0;
        
        if (*line == '\n') line++;
        
        ui_draw_text(5, y, buf);
        y += MENU_LINE_HEIGHT;
    }
    
    oled_ui_refresh();
}

// 进度显示
void ui_show_progress(const char *title, const char *message, uint32_t progress, uint32_t total, uint32_t speed) {
    oled_ui_clear();
    
    // 标题
    ui_draw_text(0, 2, title);
    
    // 状态信息
    ui_draw_text(0, 18, message);
    
    // 进度条
    if (total > 0) {
        ui_draw_progress(5, 32, OLED_WIDTH - 10, 8, progress, total);
        
        // 百分比
        ui_draw_percent(OLED_WIDTH / 2 - 12, 44, progress, total);
        
        // 速度
        char speed_buf[16];
        if (speed >= 1024) {
            snprintf(speed_buf, sizeof(speed_buf), "%luK/s", (unsigned long)(speed / 1024));
        } else {
            snprintf(speed_buf, sizeof(speed_buf), "%luB/s", (unsigned long)speed);
        }
        ui_draw_text(5, 54, speed_buf);
        
        // 字节数
        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%lu/%lu", 
                (unsigned long)progress, (unsigned long)total);
        ui_draw_text(OLED_WIDTH - 60, 54, size_buf);
    }
    
    oled_ui_refresh();
}

// 确认对话框
void ui_show_confirm(const char *message, bool *result) {
    oled_ui_clear();
    
    ui_draw_text(10, 20, message);
    
    ui_draw_text(20, 40, "确认");
    ui_draw_text(70, 40, "取消");
    
    if (*result) {
        ui_draw_char(10, 40, '>');
    } else {
        ui_draw_char(60, 40, '>');
    }
    
    oled_ui_refresh();
}

// 芯片信息显示
void ui_show_chip_info(const char *chip_name) {
    char message[64];
    snprintf(message, sizeof(message), "检测到: %s", chip_name);
    ui_show_message("芯片信息", message);
}

// 文件列表显示
void ui_show_file_list(const char *title, const char **files, int count) {
    if (count == 0) {
        ui_show_message(title, "没有文件");
        return;
    }
    
    menu_context_t menu;
    ui_menu_init(&menu, count);
    ui_menu_draw(&menu, title, files, count);
}

// 调试信息
void ui_debug(const char *text) {
    oled_ui_clear();
    ui_draw_text(0, 0, text);
    oled_ui_refresh();
}
