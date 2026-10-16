#include "imu.h"
#include "imu_config.h"
#include "imu_core.h"
#include "i2c_bus.h"
#include <string.h>
#include <stdio.h>

typedef enum { START_DELAY, RESET_SUBMIT, RESET_WAIT, RESET_DELAY,
               CONFIG_SUBMIT, CONFIG_WAIT, WHO_SUBMIT, WHO_WAIT,
               SAMPLE_SUBMIT, SAMPLE_WAIT, RETRY_WAIT } State;
static const uint8_t registers[] = { 0x6b, 0x6c, 0x19, 0x1a, 0x1b, 0x1c };
static const uint8_t values[] = { 0x01, 0x00, 9, 3, 0x08, 0x00 };
static State state;
static uint8_t config_index, tx[2], rx[14];
static uint32_t state_ms, sample_ms, calibration_ms, latest_ms;
static I2CBusRequest request;
static IMUCore core;
static IMUSample latest;
static IMUStatus status;
static bool paused;
static uint8_t address, alternate_tried;
static char fault_reason[22];

static bool Submit(uint8_t reg, uint8_t value, uint16_t read_length, uint32_t now_ms)
{
    if (I2CBus_IsBusy()) return false;
    tx[0] = reg; tx[1] = value;
    request.address = address;
    request.tx = tx;
    request.tx_length = read_length ? 1 : 2;
    request.rx = read_length ? rx : 0;
    request.rx_length = read_length;
    return I2CBus_Submit(&request, now_ms);
}
static void Fault(uint32_t now_ms, const char *stage)
{
    const char *reason = request.result == I2C_BUS_NACK ? "NACK" :
                         request.result == I2C_BUS_TIMEOUT ? "TIMEOUT" : "BUS ERROR";
    (void)snprintf(fault_reason, sizeof(fault_reason), "%s %s", stage, reason);
    latest.valid = false;
    latest.gyro_only = false;
    status = IMU_FAULT;
    state = RETRY_WAIT;
    state_ms = now_ms;
}
void IMU_Init(uint32_t now_ms)
{
    memset(&latest, 0, sizeof(latest));
    latest.gravity_age_ms = UINT32_MAX;
    memset(&request, 0, sizeof(request));
    IMUCore_Init(&core);
    state = START_DELAY;
    state_ms = now_ms;
    latest_ms = now_ms;
    status = IMU_STARTING;
    paused = false;
    address = IMU_ADDRESS; alternate_tried = 0;
    fault_reason[0] = '\0';
}
void IMU_SetPaused(bool value) { paused = value; }
IMUStatus IMU_GetStatus(void) { return status; }
uint8_t IMU_GetAddress(void) { return address; }
const char *IMU_GetFaultReason(void) { return fault_reason; }
uint16_t IMU_GetCalibrationCount(void) { return core.calibration_count; }
bool IMU_IsReady(void)
{
    return status == IMU_READY && latest.valid &&
           (uint32_t)(latest_ms - latest.timestamp_ms) <= IMU_STALE_MS;
}
bool IMU_GetSample(IMUSample *sample)
{
    if (sample == 0) return false;
    *sample = latest;
    sample->valid = latest.valid && (uint32_t)(latest_ms - latest.timestamp_ms) <= IMU_STALE_MS;
    sample->gyro_only = sample->valid && latest.gyro_only;
    return sample->valid;
}
bool IMU_RequestCalibration(void)
{
    if (state != SAMPLE_SUBMIT && state != SAMPLE_WAIT) return false;
    IMUCore_StartCalibration(&core);
    latest.calibrated = false;
    latest.gyro_only = false;
    latest.gyro_bridge_count = 0;
    latest.gravity_age_ms = UINT32_MAX;
    status = IMU_CALIBRATING;
    calibration_ms = latest_ms;
    fault_reason[0] = '\0';
    return true;
}
void IMU_Process(uint32_t now_ms)
{
    latest_ms = now_ms;
    if (paused) return;
    switch (state) {
    case START_DELAY:
        if ((uint32_t)(now_ms - state_ms) >= 100u) state = WHO_SUBMIT;
        break;
    case RESET_SUBMIT:
        if (Submit(0x6b, 0x80, 0, now_ms)) state = RESET_WAIT;
        break;
    case RESET_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        if (request.result != I2C_BUS_OK) { Fault(now_ms, "RESET"); break; }
        state_ms = now_ms; state = RESET_DELAY;
        break;
    case RESET_DELAY:
        if ((uint32_t)(now_ms - state_ms) >= 100u) { config_index = 0; state = CONFIG_SUBMIT; }
        break;
    case CONFIG_SUBMIT:
        if (Submit(registers[config_index], values[config_index], 0, now_ms)) state = CONFIG_WAIT;
        break;
    case CONFIG_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        if (request.result != I2C_BUS_OK) { Fault(now_ms, "CONFIG"); break; }
        if (++config_index == sizeof(registers)) {
            IMUCore_StartCalibration(&core);
            status = IMU_CALIBRATING;
            fault_reason[0] = '\0';
            calibration_ms = now_ms; sample_ms = now_ms - IMU_SAMPLE_MS;
            state = SAMPLE_SUBMIT;
        }
        else state = CONFIG_SUBMIT;
        break;
    case WHO_SUBMIT:
        if (Submit(0x75, 0, 1, now_ms)) state = WHO_WAIT;
        break;
    case WHO_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        /* User's module reports 0x70. Keep an explicit allowlist rather than
           bypassing identity validation for every device on this shared bus. */
        if (request.result != I2C_BUS_OK || (rx[0] != 0x68u && rx[0] != 0x70u)) {
            if (request.result == I2C_BUS_OK) {
                latest.valid = false; status = IMU_FAULT;
                state = RETRY_WAIT; state_ms = now_ms;
                (void)snprintf(fault_reason, sizeof(fault_reason), "WHO ID=%02X", (unsigned int)rx[0]);
            } else { Fault(now_ms, "WHO"); }
#if IMU_AUTO_ADDRESS
            /* Only missing/wrong devices justify trying the other address.
               Timeouts and bus errors retain the original diagnostic. */
            if (!alternate_tried && (request.result == I2C_BUS_NACK || request.result == I2C_BUS_OK)) {
                address ^= 1u; alternate_tried = 1; state = WHO_SUBMIT;
            }
#endif
            break;
        }
        status = IMU_STARTING;
        state = RESET_SUBMIT;
        break;
    case SAMPLE_SUBMIT:
        if ((uint32_t)(now_ms - sample_ms) >= IMU_SAMPLE_MS && Submit(0x3b, 0, 14, now_ms)) {
            sample_ms = now_ms; state = SAMPLE_WAIT;
        }
        break;
    case SAMPLE_WAIT:
        if (request.result == I2C_BUS_PENDING) break;
        if (request.result != I2C_BUS_OK) { Fault(now_ms, "READ"); break; }
        IMUCore_Update(&core, rx, now_ms, &latest);
        if (core.calibrated) { status = IMU_READY; fault_reason[0] = '\0'; }
        else if ((uint32_t)(now_ms - calibration_ms) >= IMU_CALIBRATION_TIMEOUT_MS) {
            /* Keep sampling for manual CAL retry, but do not claim calibrated. */
            status = IMU_FAULT;
            (void)snprintf(fault_reason, sizeof(fault_reason), "CAL KEEP STILL");
        }
        state = SAMPLE_SUBMIT;
        break;
    case RETRY_WAIT:
        if ((uint32_t)(now_ms - state_ms) >= 1000u) {
            status = IMU_STARTING; alternate_tried = 0; state = WHO_SUBMIT;
        }
        break;
    default: break;
    }
    if ((uint32_t)(now_ms - latest.timestamp_ms) > IMU_STALE_MS) latest.valid = false;
}
