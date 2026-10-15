#ifndef OLED_H
#define OLED_H
#include <stdbool.h>
#include <stdint.h>
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_8X16 8u
#define OLED_6X8 6u
void OLED_Init(uint32_t now_ms);
void OLED_Process(uint32_t now_ms);
bool OLED_IsReady(void);
bool OLED_IsBusy(void); /* Immediate work must drain before CPU WFI. */
void OLED_SetPower(bool on); /* Request; Process finishes any current transfer first. */
bool OLED_IsPowered(void);
void OLED_Clear(void);
void OLED_DrawPixel(int16_t x, int16_t y, bool on);
void OLED_DrawText(int16_t x, int16_t y, const char *text);
void OLED_DrawRect(int16_t x, int16_t y, int16_t width, int16_t height);
void OLED_FillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool on);
/* Present newest working frame. Snapshot transfer preserves each frame's contents. */
void OLED_Present(void);
/* Compatible drawing subset from the user's supplied Jiangxie OLED library.
 * Drawing touches RAM only; OLED_Update requests an asynchronous transfer. */
void OLED_Update(void);
void OLED_ShowChar(uint8_t x, uint8_t y, char ch, uint8_t font_size);
void OLED_ShowString(uint8_t x, uint8_t y, const char *text, uint8_t font_size);
void OLED_ShowChinese(uint8_t x, uint8_t y, const char *utf8);
void OLED_ShowImage(uint8_t x, uint8_t y, uint8_t width, uint8_t height, const uint8_t *image);
#endif
