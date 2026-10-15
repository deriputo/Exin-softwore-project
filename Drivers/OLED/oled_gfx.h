#ifndef OLED_GFX_H
#define OLED_GFX_H
#include <stdint.h>
#include <stdbool.h>
void OLEDGfx_Pixel(uint8_t buffer[1024], int16_t x, int16_t y, bool on);
void OLEDGfx_Text(uint8_t buffer[1024], int16_t x, int16_t y, const char *text);
void OLEDGfx_Rect(uint8_t buffer[1024], int16_t x, int16_t y, int16_t w, int16_t h, bool fill, bool on);
void OLEDGfx_Image(uint8_t buffer[1024], int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *image);
void OLEDGfx_Char(uint8_t buffer[1024], int16_t x, int16_t y, char ch, uint8_t size);
void OLEDGfx_Chinese(uint8_t buffer[1024], int16_t x, int16_t y, const char *utf8);
#endif
