#include "oled.h"
#include "oled_config.h"
#include "oled_gfx.h"
#include "i2c_bus.h"
#include <string.h>

static const uint8_t init_commands[] = {
    0x00,0xae,0xd5,0x80,0xa8,0x3f,0xd3,0x00,0x40,0x8d,0x14,
    0x20,0x02,0xa1,0xc8,0xda,0x12,0x81,0xcf,0xd9,0xf1,
    0xdb,0x30,0xa4,0xa6,0xaf
};
typedef enum { INIT_DELAY, INIT_SEND, INIT_WAIT, IDLE, ADDRESS_SEND,
               ADDRESS_WAIT, DATA_SEND, DATA_WAIT, POWER_WAIT, RETRY } State;
static State state;
static uint8_t frame[1024], snapshot[1024], tx[OLED_CHUNK_SIZE + 1u];
static I2CBusRequest request;
static uint16_t offset;
static uint32_t state_ms;
static bool ready, requested_power, powered, pending, transferring;

static bool Submit(const uint8_t *data, uint16_t length, uint32_t now_ms)
{
    if (I2CBus_IsBusy()) return false;
    request.address = OLED_ADDRESS;
    request.tx = data; request.tx_length = length;
    request.rx = 0; request.rx_length = 0;
    return I2CBus_Submit(&request, now_ms);
}
static void Fault(uint32_t now_ms)
{
    ready = false; powered = false; transferring = false; pending = true;
    state_ms = now_ms; state = RETRY;
}
void OLED_Init(uint32_t now_ms)
{
    memset(frame, 0, sizeof(frame));
    memset(snapshot, 0, sizeof(snapshot));
    memset(&request, 0, sizeof(request));
    state = INIT_DELAY; state_ms = now_ms;
    ready = false; powered = false; requested_power = true;
    pending = true; transferring = false; offset = 0;
}
bool OLED_IsReady(void) { return ready; }
bool OLED_IsBusy(void)
{
    if (state == INIT_DELAY || state == RETRY) return false;
    if (state == IDLE) return requested_power != powered || (powered && pending);
    return true;
}
bool OLED_IsPowered(void) { return ready && powered; }
void OLED_SetPower(bool on) { requested_power = on; }
void OLED_Clear(void) { memset(frame, 0, sizeof(frame)); }
void OLED_DrawPixel(int16_t x, int16_t y, bool on) { OLEDGfx_Pixel(frame, x, y, on); }
void OLED_DrawText(int16_t x, int16_t y, const char *text) { OLEDGfx_Text(frame, x, y, text); }
void OLED_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h) { OLEDGfx_Rect(frame, x, y, w, h, false, true); }
void OLED_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) { OLEDGfx_Rect(frame, x, y, w, h, true, on); }
void OLED_Present(void) { pending = true; }
void OLED_Update(void) { OLED_Present(); }
void OLED_ShowChar(uint8_t x, uint8_t y, char ch, uint8_t size) { OLEDGfx_Char(frame, x, y, ch, size); }
void OLED_ShowString(uint8_t x, uint8_t y, const char *text, uint8_t size)
{
    int16_t at = x;
    uint8_t width = size == OLED_8X16 ? 8u : 6u;
    if (text == 0) return;
    while (*text && at < OLED_WIDTH) { OLEDGfx_Char(frame, at, y, *text++, width); at += width; }
}
void OLED_ShowChinese(uint8_t x, uint8_t y, const char *utf8) { OLEDGfx_Chinese(frame, x, y, utf8); }
void OLED_ShowImage(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *image) { OLEDGfx_Image(frame, x, y, w, h, image); }

void OLED_Process(uint32_t now_ms)
{
    switch (state) {
    case INIT_DELAY:
        if ((uint32_t)(now_ms - state_ms) >= 100u) state = INIT_SEND;
        break;
    case INIT_SEND:
        if (Submit(init_commands, sizeof(init_commands), now_ms)) state = INIT_WAIT;
        break;
    case INIT_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        if (request.result != I2C_BUS_OK) { Fault(now_ms); break; }
        ready = true; powered = true; state = IDLE;
        break;
    case IDLE:
        if (requested_power != powered) {
            tx[0] = 0x00; tx[1] = requested_power ? 0xaf : 0xae;
            if (Submit(tx, 2, now_ms)) state = POWER_WAIT;
        } else if (powered && pending) {
            memcpy(snapshot, frame, sizeof(frame));
            pending = false; transferring = true; offset = 0; state = ADDRESS_SEND;
        }
        break;
    case ADDRESS_SEND:
        /* Power off is allowed at a chunk boundary for quick low power entry. */
        if (!requested_power) { transferring = false; pending = true; state = IDLE; break; }
        tx[0] = 0x00;
        tx[1] = (uint8_t)(0xb0u + offset / 128u);
        tx[2] = (uint8_t)(offset & 0x0fu);
        tx[3] = (uint8_t)(0x10u | ((offset % 128u) >> 4));
        if (Submit(tx, 4, now_ms)) state = ADDRESS_WAIT;
        break;
    case ADDRESS_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        if (request.result != I2C_BUS_OK) { Fault(now_ms); break; }
        state = DATA_SEND;
        break;
    case DATA_SEND:
        tx[0] = 0x40;
        memcpy(tx + 1, snapshot + offset, OLED_CHUNK_SIZE);
        if (Submit(tx, OLED_CHUNK_SIZE + 1u, now_ms)) state = DATA_WAIT;
        break;
    case DATA_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        if (request.result != I2C_BUS_OK) { Fault(now_ms); break; }
        offset += OLED_CHUNK_SIZE;
        if (offset == sizeof(snapshot)) { transferring = false; state = IDLE; }
        else state = ADDRESS_SEND;
        break;
    case POWER_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        if (request.result != I2C_BUS_OK) { Fault(now_ms); break; }
        powered = tx[1] == 0xaf;
        if (powered) pending = true;
        state = IDLE;
        break;
    case RETRY:
        if ((uint32_t)(now_ms - state_ms) >= 1000u) state = INIT_SEND;
        break;
    default: break;
    }
    (void)transferring;
}
