// font_data.c - 测试版
#include "stm_programmer.h"

typedef struct {
    uint16_t unicode;
    uint8_t data[24];
} chinese_font_12x12_t;

// 只保留一个"开"字测试
static const chinese_font_12x12_t chinese_font_table[] = {
    {0x5F00, {
        0x01,0x00,
        0x01,0x00,
        0x01,0x00,
        0x01,0x00,
        0x7F,0xF0,
        0x01,0x00,
        0x01,0x00,
        0x01,0x00,
        0x01,0x00,
        0x01,0x00,
        0x01,0x00,
        0x00,0x00
    }},
};

#define CHINESE_FONT_COUNT 1

bool font_get_chinese(uint16_t unicode, uint8_t data[24]) {
    if (unicode == 0x5F00) {
        memcpy(data, chinese_font_table[0].data, 24);
        return true;
    }
    return false;
}
