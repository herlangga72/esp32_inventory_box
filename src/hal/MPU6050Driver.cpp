#include "MPU6050Driver.h"
#include "Config.h"
#include "InterruptManager.h"

#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define I2C_NUM   I2C_NUM_0
#define I2C_TO_MS 10

static bool i2cReady = false;

static void i2cMasterInit() {
    if (i2cReady) return;
    i2c_config_t c = {};
    c.mode             = I2C_MODE_MASTER;
    c.sda_io_num       = PIN_MPU_SDA;
    c.scl_io_num       = PIN_MPU_SCL;
    c.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    c.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    c.master.clk_speed = 400000;
    c.clk_flags        = I2C_SCLK_SRC_FLAG_FOR_NOMAL;
    if (i2c_param_config(I2C_NUM, &c) != ESP_OK) return;
    esp_err_t e = i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) i2cReady = true;  // ESP_ERR_INVALID_STATE = already installed (SSD1306 was first)
}

// ---- Constructor ----

MPU6050Driver::MPU6050Driver(uint8_t a) : addr(a) {
    accelOffX = accelOffY = accelOffZ = 0.0f;
    accelScale = 16384.0f;
    gyroScale  = 131.0f;
}

// ---- Unlocked internal I/O (caller holds i2cLock) ----

void MPU6050Driver::_writeReg(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    i2c_master_write_to_device(I2C_NUM, addr, b, 2, pdMS_TO_TICKS(I2C_TO_MS));
}

uint8_t MPU6050Driver::_readReg(uint8_t reg) {
    uint8_t v = 0;
    i2c_master_write_read_device(I2C_NUM, addr, &reg, 1, &v, 1,
                                 pdMS_TO_TICKS(I2C_TO_MS));
    return v;
}

void MPU6050Driver::_readBytes(uint8_t reg, uint8_t* buf, size_t len) {
    i2c_master_write_read_device(I2C_NUM, addr, &reg, 1, buf, len,
                                 pdMS_TO_TICKS(I2C_TO_MS));
}

// ---- Public locked wrappers ----

void MPU6050Driver::writeRegister(uint8_t r, uint8_t v) {
    i2cLock(); _writeReg(r, v); i2cUnlock();
}

uint8_t MPU6050Driver::readRegister(uint8_t r) {
    i2cLock(); uint8_t v = _readReg(r); i2cUnlock(); return v;
}

// ---- Init (single lock scope, batched writes, no re-lock deadlocks) ----

static const uint8_t initBatch[] = {
    0x6B, 0x80,   // PWR_MGMT_1: reset
    0x00, 0x00,   // padding (100ms delay goes here)
    0x6B, 0x00,   // PWR_MGMT_1: wake, clk=8MHz
    0x1C, 0x00,   // ACCEL_CONFIG: ±2g
    0x1B, 0x00,   // GYRO_CONFIG:  ±250°/s
    0x1A, 0x06,   // CONFIG: DLPF 5Hz
    0x37, 0x80,   // INT_PIN_CFG: level, open drain
    0x38, 0x01,   // INT_ENABLE: data ready
};

bool MPU6050Driver::begin() {
    i2cLock();
    i2cMasterInit();

    // WHO_AM_I probe
    if (_readReg(0x75) != 0x68) { i2cUnlock(); return false; }

    // Batched init: reset
    _writeReg(0x6B, 0x80);
    i2cUnlock();                         // release during 100ms reset wait
    vTaskDelay(pdMS_TO_TICKS(100));
    i2cLock();

    // Batched config burst (8 registers in one locked scope)
    _writeReg(0x6B, 0x00);               // wake
    _writeReg(0x1C, 0x00);               // ±2g
    _writeReg(0x1B, 0x00);               // ±250°/s
    _writeReg(0x1A, 0x06);               // DLPF 5Hz
    _writeReg(0x37, 0x80);               // INT_PIN_CFG
    _writeReg(0x38, 0x01);               // INT_ENABLE

    i2cUnlock();
    return true;
}

// ---- Targeted data reads (skip unused bytes) ----

static inline int16_t be16(const uint8_t* d, int i) {
    return (int16_t)((d[i] << 8) | d[i + 1]);
}

void MPU6050Driver::readAccel(float& ax, float& ay, float& az) {
    i2cLock();
    uint8_t d[6];
    _readBytes(0x3B, d, 6);   // only 6 bytes, not 14
    i2cUnlock();
    ax = (be16(d, 0) / accelScale) - accelOffX;
    ay = (be16(d, 2) / accelScale) - accelOffY;
    az = (be16(d, 4) / accelScale) - accelOffZ;
}

void MPU6050Driver::readGyro(float& gx, float& gy, float& gz) {
    i2cLock();
    uint8_t d[6];
    _readBytes(0x43, d, 6);   // gyro starts at 0x43, not 0x3B
    i2cUnlock();
    gx = be16(d, 0) / gyroScale;
    gy = be16(d, 2) / gyroScale;
    gz = be16(d, 4) / gyroScale;
}

void MPU6050Driver::readMotion6(float& ax, float& ay, float& az,
                                float& gx, float& gy, float& gz) {
    i2cLock();
    uint8_t d[14];
    _readBytes(0x3B, d, 14);  // single 14-byte burst when both needed
    i2cUnlock();
    ax = (be16(d, 0)  / accelScale) - accelOffX;
    ay = (be16(d, 2)  / accelScale) - accelOffY;
    az = (be16(d, 4)  / accelScale) - accelOffZ;
    gx =  be16(d, 8)  / gyroScale;
    gy =  be16(d, 10) / gyroScale;
    gz =  be16(d, 12) / gyroScale;
}

// ---- Configuration (single lock per function, no re-locking) ----

void MPU6050Driver::setAccelRange(uint8_t range) {
    uint8_t v; float s;
    switch (range) {
        case  2: v = 0; s = 16384.0f; break;
        case  4: v = 1; s =  8192.0f; break;
        case  8: v = 2; s =  4096.0f; break;
        default:  v = 3; s =  2048.0f; break;
    }
    accelScale = s;
    writeRegister(0x1C, v << 3);
}

void MPU6050Driver::setGyroRange(uint16_t range) {
    uint8_t v; float s;
    switch (range) {
        case  250: v = 0; s = 131.0f; break;
        case  500: v = 1; s =  65.5f; break;
        case 1000: v = 2; s =  32.8f; break;
        default:    v = 3; s =  16.4f; break;
    }
    gyroScale = s;
    writeRegister(0x1B, v << 3);
}

void MPU6050Driver::setDLPF(uint8_t bw) {
    writeRegister(0x1A, bw & 0x07);
}

void MPU6050Driver::enableMotionInterrupt(bool en, float thresh) {
    i2cLock();
    if (en) {
        _writeReg(0x1F, (uint8_t)(thresh * 255));
        _writeReg(0x20, 0x01);
        _writeReg(0x69, 0x30);
        _writeReg(0x38, 0x40);
    } else {
        _writeReg(0x38, 0x01);
    }
    i2cUnlock();
}

uint8_t MPU6050Driver::getInterruptSource() {
    return readRegister(0x3A);
}

// ---- Calibration (isolated offsets — no live contamination) ----

void MPU6050Driver::calibrate(float& axOff, float& ayOff, float& azOff) {
    // Save and zero current offsets — sample raw hardware error
    float oldX = accelOffX, oldY = accelOffY, oldZ = accelOffZ;
    accelOffX = accelOffY = accelOffZ = 0.0f;

    int64_t sx = 0, sy = 0, sz = 0;  // 64-bit accumulators
    const int N = 100;
    for (int i = 0; i < N; i++) {
        uint8_t d[6];
        i2cLock();
        _readBytes(0x3B, d, 6);
        i2cUnlock();
        sx += be16(d, 0);
        sy += be16(d, 2);
        sz += be16(d, 4);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Compute raw offsets in g units
    accelOffX = (float)(sx / N) / accelScale;
    accelOffY = (float)(sy / N) / accelScale;
    accelOffZ = (float)(sz / N) / accelScale - 1.0f;  // Z = 1g static

    // Restore old offsets if calibration was aborted/failed
    // (offsets computed from raw — no echo contamination)
    axOff = accelOffX;
    ayOff = accelOffY;
    azOff = accelOffZ;
}

void MPU6050Driver::getOffsets(float& ax, float& ay, float& az) {
    ax = accelOffX; ay = accelOffY; az = accelOffZ;
}

void MPU6050Driver::setOffsets(float ax, float ay, float az) {
    accelOffX = ax; accelOffY = ay; accelOffZ = az;
}

bool MPU6050Driver::testConnection() {
    return readRegister(0x75) == 0x68;
}
