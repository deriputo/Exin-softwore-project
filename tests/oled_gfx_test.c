#include "oled_gfx.h"
#include "OLED_Data.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint8_t guarded[1040];
    uint8_t *buffer = guarded + 8;
    unsigned int i;
    memset(guarded, 0xa5, sizeof(guarded)); memset(buffer, 0, 1024);
    OLEDGfx_Pixel(buffer, 127, 63, true);
    assert(buffer[1023] == 0x80);
    OLEDGfx_Pixel(buffer, 127, 63, false);
    assert(buffer[1023] == 0);
    OLEDGfx_Pixel(buffer, -1, 0, true); OLEDGfx_Pixel(buffer, 128, 64, true);
    OLEDGfx_Text(buffer, 0, 0, "Aa0");
    for (i = 0; i < 6; ++i) {
        assert(buffer[i] == OLED_F6x8['A' - 32][i]);
        assert(buffer[i + 6] == OLED_F6x8['a' - 32][i]);
        assert(buffer[i + 12] == OLED_F6x8['0' - 32][i]);
    }
    memset(buffer, 0, 1024);
    OLEDGfx_Char(buffer, 3, 5, 'A', 8);
    assert(buffer[3] == (uint8_t)(OLED_F8x16['A' - 32][0] << 5));
    memset(buffer, 0, 1024);
    OLEDGfx_Chinese(buffer, 0, 0, OLED_CF16x16[0].Index);
    for (i = 0; i < 16; ++i) {
        assert(buffer[i] == OLED_CF16x16[0].Data[i]);
        assert(buffer[128 + i] == OLED_CF16x16[0].Data[16 + i]);
    }
    OLEDGfx_Rect(buffer, -300, -300, 32767, 32767, true, true);
    for (i = 0; i < 1024; ++i) assert(buffer[i] == 0xff);
    OLEDGfx_Rect(buffer, 125, 61, 20, 20, true, false);
    OLEDGfx_Text(buffer, 125, 62, "Clipped");
    OLEDGfx_Chinese(buffer, 126, 60, "\xe4\xb8"); /* Truncated UTF-8. */
    for (i = 0; i < 8; ++i) { assert(guarded[i] == 0xa5); assert(guarded[1032 + i] == 0xa5); }
    puts("OLED original-font rendering, image layout, UTF-8 and clipping tests passed");
    return 0;
}
