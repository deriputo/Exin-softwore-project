#include "i2c_bus.h"
#include "imu.h"
#include "oled.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static I2CBusRequest *active;
static unsigned int imu_reads, oled_data_chunks, oled_commands;
static bool fail_imu;
static bool fail_oled_data;
static uint8_t device_address = 0x68, device_id = 0x68;
static I2CBusResult failure = I2C_BUS_NACK;
static unsigned int imu_writes;
static uint8_t display[1024];
static uint16_t display_at;
void I2CBus_Init(void) { active = 0; }
bool I2CBus_IsBusy(void) { return active != 0; }
bool I2CBus_Suspend(void) { return active == 0; }
void I2CBus_Resume(void) { }
bool I2CBus_Submit(I2CBusRequest *req, uint32_t now)
{
    (void)now;
    if (active) return false;
    assert(req && req->tx && req->tx_length);
    active = req; req->result = I2C_BUS_PENDING; return true;
}
void I2CBus_Process(uint32_t now)
{
    (void)now;
    if (!active) return;
    if (active->address == 0x68 || active->address == 0x69) {
        if (fail_imu || (device_address && active->address != device_address)) {
            active->result = fail_imu ? failure : I2C_BUS_NACK; active = 0; return;
        }
        if (active->rx_length == 1) { assert(active->tx[0] == 0x75); active->rx[0] = device_id; }
        else if (active->rx_length == 14) {
            assert(active->tx[0] == 0x3b);
            memset(active->rx, 0, 14); active->rx[2] = 0xc0; /* Actual mount: sensor Y=-1g. */
            ++imu_reads;
        }
        else { ++imu_writes; assert(device_id == 0x68 || device_id == 0x70); }
    } else {
        assert(active->address == 0x3c);
        if (active->tx[0] == 0x40) {
            if (fail_oled_data) {
                fail_oled_data = false;
                active->result = I2C_BUS_NACK; active = 0; return;
            }
            assert(active->tx_length == 17 && display_at <= 1008);
            memcpy(display + display_at, active->tx + 1, 16);
            ++oled_data_chunks;
        } else if (active->tx_length == 4) {
            display_at = (uint16_t)((active->tx[1] & 7u) * 128u +
                                   (active->tx[2] & 15u) + ((active->tx[3] & 15u) << 4));
        } else ++oled_commands;
    }
    active->result = I2C_BUS_OK; active = 0;
}
static void Tick(uint32_t now)
{
    I2CBus_Process(now); IMU_Process(now); OLED_Process(now);
}
int main(void)
{
    IMUSample sample;
    uint32_t now;
    unsigned int chunks;
    I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 3000; ++now) Tick(now);
    assert(IMU_IsReady() && IMU_GetSample(&sample) && sample.calibrated);
    assert(imu_reads >= 200 && OLED_IsReady() && OLED_IsPowered());
    assert(oled_data_chunks == 64); /* Initial RAM is unknown, including zero bytes. */
    OLED_Clear(); OLED_DrawPixel(0, 0, true); OLED_Present();
    chunks = oled_data_chunks;
    Tick(now++); /* Copy snapshot on IDLE. */
    OLED_Clear(); OLED_DrawPixel(127, 63, true); OLED_Present();
    while (oled_data_chunks < chunks + 1 && now < 5000) Tick(now++);
    assert(display[0] == 1 && display[1023] == 0); /* First snapshot never mutated. */
    while (OLED_IsBusy() && now < 6000) Tick(now++);
    assert(oled_data_chunks == chunks + 3); /* One chunk, then erase + draw two chunks. */
    assert(display[0] == 0 && display[1023] == 0x80); /* Newest next frame preserved. */
    chunks = oled_data_chunks;
    OLED_Present();
    Tick(now++);
    assert(!OLED_IsBusy() && oled_data_chunks == chunks); /* Identical frame: no writes. */
    OLED_Clear(); OLED_Present();
    while (OLED_IsBusy() && now < 6000) Tick(now++);
    assert(oled_data_chunks == chunks + 1 && display[1023] == 0); /* Erasure is dirty too. */
    OLED_SetPower(false);
    while (OLED_IsPowered() && now < 6500) Tick(now++);
    assert(!OLED_IsPowered() && OLED_IsReady());
    chunks = oled_data_chunks;
    for (; now < 7000; ++now) Tick(now);
    assert(oled_data_chunks == chunks && IMU_IsReady()); /* OLED off leaves IMU alive. */
    IMU_SetPaused(true); Tick(now += 200);
    assert(!IMU_GetSample(&sample) && !IMU_IsReady());
    IMU_SetPaused(false); fail_imu = true;
    for (; now < 7500; ++now) Tick(now);
    assert(IMU_GetStatus() == IMU_FAULT && !IMU_GetSample(&sample));
    assert(strcmp(IMU_GetFaultReason(), "READ NACK") == 0 ||
           strcmp(IMU_GetFaultReason(), "WHO NACK") == 0);
    fail_imu = false;
    for (; now < 11000; ++now) Tick(now);
    assert(IMU_IsReady() && IMU_GetSample(&sample)); /* Peripheral failure retries/calibrates. */
    assert(IMU_RequestCalibration() && IMU_GetStatus() == IMU_CALIBRATING);
    chunks = oled_data_chunks;
    OLED_SetPower(true);
    for (; now < 14000; ++now) Tick(now);
    assert(IMU_IsReady() && OLED_IsPowered() && oled_commands >= 3);
    assert(oled_data_chunks == chunks + 64); /* Wake always restores the entire image. */

    /* A failed chunk cannot become the trusted display image. Reinitialization
       must restore even unchanged/zero regions from unknown display RAM. */
    OLED_DrawPixel(16, 8, true); OLED_Present(); fail_oled_data = true;
    for (; now < 14500; ++now) Tick(now);
    assert(!OLED_IsReady());
    memset(display, 0xff, sizeof(display));
    chunks = oled_data_chunks;
    for (; now < 17000; ++now) Tick(now);
    assert(OLED_IsReady() && !OLED_IsBusy() && IMU_IsReady());
    assert(oled_data_chunks == chunks + 64);
    for (chunks = 0; chunks < sizeof(display); ++chunks)
        assert(display[chunks] == (chunks == 144 ? 1 : 0));

    /* Interrupt a multi-chunk refresh to blank promptly. Latest drawing while
       blanked must replace the interrupted snapshot on wake. */
    chunks = oled_data_chunks;
    OLED_FillRect(0, 0, 128, 64, true); OLED_Present();
    while (oled_data_chunks == chunks && now < 17500) Tick(now++);
    assert(oled_data_chunks > chunks);
    OLED_SetPower(false);
    while (OLED_IsPowered() && now < 18000) Tick(now++);
    assert(!OLED_IsPowered() && oled_data_chunks < chunks + 64);
    OLED_Clear(); OLED_DrawPixel(127, 63, true); OLED_Present();
    chunks = oled_data_chunks;
    for (; now < 18500; ++now) Tick(now);
    assert(oled_data_chunks == chunks);
    OLED_SetPower(true);
    for (; now < 19500; ++now) Tick(now);
    assert(OLED_IsPowered() && !OLED_IsBusy());
    assert(oled_data_chunks == chunks + 64);
    for (chunks = 0; chunks < sizeof(display); ++chunks)
        assert(display[chunks] == (chunks == 1023 ? 0x80 : 0));

    /* AD0 high: missing 0x68 is probed read-only, then 0x69 works. */
    device_address = 0x69; I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 3000; ++now) Tick(now);
    assert(IMU_IsReady() && IMU_GetAddress() == 0x69 && !IMU_GetFaultReason()[0]);

    /* User's replacement module: WHO_AM_I=0x70 at either legal I2C address. */
    device_id = 0x70; device_address = 0x68; imu_writes = 0;
    I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 3000; ++now) Tick(now);
    assert(IMU_IsReady() && IMU_GetAddress() == 0x68 && imu_writes == 7);
    assert(IMU_GetSample(&sample) && sample.calibrated && sample.valid);
    device_address = 0x69; imu_writes = 0;
    I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 3000; ++now) Tick(now);
    assert(IMU_IsReady() && IMU_GetAddress() == 0x69 && imu_writes == 7);
    device_id = 0x68;

    /* Fault reason survives the one-second retry, including its STARTING phase. */
    fail_imu = true; I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 500; ++now) Tick(now);
    assert(IMU_GetStatus() == IMU_FAULT && strcmp(IMU_GetFaultReason(), "WHO NACK") == 0);
    for (; now < 1110; ++now) Tick(now);
    assert(strcmp(IMU_GetFaultReason(), "WHO NACK") == 0);
    fail_imu = false;
    for (; now < 4500; ++now) Tick(now);
    assert(IMU_IsReady() && IMU_GetAddress() == 0x69);

    failure = I2C_BUS_TIMEOUT; fail_imu = true;
    I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 500; ++now) Tick(now);
    assert(IMU_GetAddress() == 0x68 && strcmp(IMU_GetFaultReason(), "WHO TIMEOUT") == 0);
    failure = I2C_BUS_ERROR;
    I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 500; ++now) Tick(now);
    assert(strcmp(IMU_GetFaultReason(), "WHO BUS ERROR") == 0);

    /* An unexpected chip ID must never receive reset/config writes. */
    fail_imu = false; device_address = 0; device_id = 0x71; imu_writes = 0;
    I2CBus_Init(); IMU_Init(0); OLED_Init(0);
    for (now = 0; now < 500; ++now) Tick(now);
    assert(IMU_GetStatus() == IMU_FAULT && !imu_writes && !IMU_GetSample(&sample));
    assert(strcmp(IMU_GetFaultReason(), "WHO ID=71") == 0);
    puts("PASS: shared-bus IMU/OLED, 0x68/0x69 discovery, retained NACK/timeout/bus/ID diagnostics, no writes to wrong chip");
    return 0;
}
