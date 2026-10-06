#ifndef OLED_UI_H
#define OLED_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "stm_programmer.h"

// OLED显示配置
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDR       0x3C

// 字体配置
#define FONT_ASCII_WIDTH  6
#define FONT_ASCII_HEIGHT 8
#define FONT_CHINESE_WIDTH 12
#define FONT_CHINESE_HEIGHT 12
#define MENU_LINE_HEIGHT   14

// 显示缓冲区
extern uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];

// 基础功能
bool oled_ui_init(void);
void oled_ui_clear(void);
void oled_ui_refresh(void);

// 文本显示
void ui_draw_text(int x, int y, const char *text);
void ui_draw_char(int x, int y, char c);
void ui_draw_number(int x, int y, uint32_t num);

// 图形元素
void ui_draw_line(int x1, int y1, int x2, int y2);
void ui_draw_rect(int x, int y, int w, int h, bool fill);
void ui_draw_progress(int x, int y, int w, int h, uint32_t value, uint32_t total);
void ui_draw_percent(int x, int y, uint32_t value, uint32_t total);

// 菜单系统
typedef struct {
    int selected;
    int scroll;
    int total_items;
    int items_per_page;
    int start_y;
    int end_y;
} menu_context_t;

void ui_menu_init(menu_context_t *menu, int total_items);
void ui_menu_draw(menu_context_t *menu, const char *title, const char **items, int item_count);
void ui_menu_navigate(menu_context_t *menu, key_event_t key, int *selected_item);

// 页面显示
void ui_show_startup(void);
void ui_show_main_menu(void);
void ui_show_message(const char *title, const char *message);
void ui_show_progress(const char *title, const char *message, uint32_t progress, uint32_t total, uint32_t speed);
void ui_show_confirm(const char *message, bool *result);

// 状态显示
void ui_show_chip_info(const char *chip_name);
void ui_show_file_list(const char *title, const char **files, int count);

// 调试
void ui_debug(const char *text);

#endif // OLED_UI_H