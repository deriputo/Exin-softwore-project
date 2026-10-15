/* Adapted from user provided OLED_Data.h. Jiangxie Technology owns original data. */
#ifndef OLED_DATA_H
#define OLED_DATA_H
#include <stdint.h>
#define OLED_CHN_CHAR_WIDTH 3
typedef struct {
    uint8_t Data[32];
    char Index[OLED_CHN_CHAR_WIDTH + 1];
} ChineseCell_t;
extern const uint8_t OLED_F8x16[][16];
extern const uint8_t OLED_F6x8[][6];
extern const ChineseCell_t OLED_CF16x16[];
#endif
