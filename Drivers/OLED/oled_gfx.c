#include "oled_gfx.h"
/* Framebuffer/font layout adapted from user supplied Jiangxie Technology OLED driver. Original font ownership retained. */
#include "OLED_Data.h"
#include <string.h>

void OLEDGfx_Pixel(uint8_t buffer[1024], int16_t x, int16_t y, bool on)
{
    uint16_t index;
    uint8_t mask;
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    index = (uint16_t)((y / 8) * 128 + x);
    mask = (uint8_t)(1u << (y % 8));
    if (on) buffer[index] |= mask;
    else buffer[index] &= (uint8_t)~mask;
}
void OLEDGfx_Text(uint8_t buffer[1024], int16_t x, int16_t y, const char *text)
{
    int16_t cx = x, cy = y;
    unsigned int col, row;
    uint8_t c;
    if (text == 0) return;
    while (*text) {
        c = (uint8_t)*text++;
        if (c == '\n') { cx = x; cy += 8; continue; }
        if (c < 32 || c > 126) c = '?';
        for (col = 0; col < 6; ++col) {
            uint8_t bits = OLED_F6x8[c - 32][col];
            for (row = 0; row < 8; ++row)
                OLEDGfx_Pixel(buffer, (int16_t)(cx + col), (int16_t)(cy + row),
                              (bits & (1u << row)) != 0);
        }
        cx += 6;
        if (cx >= 128 || cy >= 64) break;
    }
}
void OLEDGfx_Rect(uint8_t buffer[1024], int16_t x, int16_t y, int16_t w, int16_t h, bool fill, bool on)
{
    int32_t left = x, top = y, right = (int32_t)x + w, bottom = (int32_t)y + h;
    int32_t xx, yy;
    if (w <= 0 || h <= 0) return;
    /* Clip before loops: extreme input cannot cause unbounded offscreen work. */
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > 128) right = 128;
    if (bottom > 64) bottom = 64;
    for (yy = top; yy < bottom; ++yy)
        for (xx = left; xx < right; ++xx)
            if (fill || xx == x || xx == (int32_t)x + w - 1 || yy == y || yy == (int32_t)y + h - 1)
                OLEDGfx_Pixel(buffer, (int16_t)xx, (int16_t)yy, on);
}

void OLEDGfx_Image(uint8_t buffer[1024], int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *image)
{
    uint16_t col, row;
    if (image == 0) return;
    for (row = 0; row < h && row < 64u; ++row)
        for (col = 0; col < w && col < 128u; ++col)
            OLEDGfx_Pixel(buffer, (int16_t)(x + col), (int16_t)(y + row),
                          (image[(row / 8u) * w + col] & (1u << (row % 8u))) != 0);
}
void OLEDGfx_Char(uint8_t buffer[1024], int16_t x, int16_t y, char ch, uint8_t size)
{
    uint8_t c = (uint8_t)ch;
    if (c < 32 || c > 126) c = '?';
    if (size == 8u) OLEDGfx_Image(buffer, x, y, 8, 16, OLED_F8x16[c - 32]);
    else OLEDGfx_Image(buffer, x, y, 6, 8, OLED_F6x8[c - 32]);
}
void OLEDGfx_Chinese(uint8_t buffer[1024], int16_t x, int16_t y, const char *utf8)
{
    size_t i;
    int16_t at = x;
    if (utf8 == 0) return;
    while (*utf8 && at < 128) {
        if ((uint8_t)*utf8 < 0x80u) {
            OLEDGfx_Char(buffer, at, y, *utf8++, 8);
            at += 8;
            continue;
        }
        /* The supplied glyph table contains UTF-8 three-byte CJK characters. */
        if (!utf8[1] || !utf8[2] || ((uint8_t)utf8[0] & 0xf0u) != 0xe0u ||
            ((uint8_t)utf8[1] & 0xc0u) != 0x80u || ((uint8_t)utf8[2] & 0xc0u) != 0x80u) {
            OLEDGfx_Char(buffer, at, y, '?', 8); ++utf8; at += 8; continue;
        }
        for (i = 0; OLED_CF16x16[i].Index[0]; ++i)
            if (memcmp(utf8, OLED_CF16x16[i].Index, 3) == 0) break;
        OLEDGfx_Image(buffer, at, y, 16, 16, OLED_CF16x16[i].Data);
        utf8 += 3; at += 16;
    }
}
