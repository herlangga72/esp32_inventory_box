#include "MPU6050Driver.h"
#include "Config.h"
#include "InterruptManager.h"

MPU6050Driver::MPU6050Driver(uint8_t addr) : addr(addr) {
    accelOffX = accelOffY = accelOffZ = 0.0f;
    accelScale = 16384.0f;  // Default ±2g
}

bool MPU6050Driver::begin() {
    i2cLock();
    Wire.begin(PIN_MPU_SDA, PIN_MPU_SCL);
    Wire.setClock(400000);
    Wire.setTimeOut(50);

    // Check WHO_AM_I
    uint8_t whoami = readRegister(0x75);
    if (whoami != 0x68) {
        Wire.end();
        i2cUnlock();
        return false;
    }
    
    // Reset device
    writeRegister(0x6B, 0x80);  // PWR_MGMT_1: reset
    delay(100);
    
    // Wake up
    writeRegister(0x6B, 0x00);   // PWR_MGMT_1: wake
    
    // Configure accelerometer (±2g)
    setAccelRange(2);
    
    // Configure gyroscope (±250°/s)
    setGyroRange(250);
    
    // Configure DLPF (5Hz bandwidth for low power)
    setDLPF(6);
    
    // Enable interrupt pin
    writeRegister(0x37, 0x80);   // INT_PIN_CFG: open drain, active low
    writeRegister(0x38, 0x01);  // INT_ENABLE: data ready enable

    i2cUnlock();
    return true;
}

void MPU6050Driver::readRaw(int16_t& ax, int16_t& ay, int16_t& az,
                           int16_t& gx, int16_t& gy, int16_t& gz) {
    i2cLock();
    Wire.beginTransmission(addr);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, 14);

    ax = Wire.read() << 8 | Wire.read();
    ay = Wire.read() << 8 | Wire.read();
    az = Wire.read() << 8 | Wire.read();
    // Skip temperature
    Wire.read(); Wire.read();
    gx = Wire.read() << 8 | Wire.read();
    gy = Wire.read() << 8 | Wire.read();
    gz = Wire.read() << 8 | Wire.read();
    i2cUnlock();
}

void MPU6050Driver::readAccel(float& ax, float& ay, float& az) {
    int16_t rawAx, rawAy, rawAz;
    readRaw(rawAx, rawAy, rawAz, rawAx, rawAy, rawAz);
    
    ax = (rawAx / accelScale) - accelOffX;
    ay = (rawAy / accelScale) - accelOffY;
    az = (rawAz / accelScale) - accelOffZ;
}

void MPU6050Driver::readGyro(float& gx, float& gy, float& gz) {
    int16_t rawGx, rawGy, rawGz;
    readRaw(rawGx, rawGy, rawGz, rawGx, rawGy, rawGz);
    
    gx = rawGx / gyroScale;
    gy = rawGy / gyroScale;
    gz = rawGz / gyroScale;
}

void MPU6050Driver::setAccelRange(uint8_t range) {
    uint8_t value;
    switch (range) {
        case 2:  value = 0; accelScale = 16384.0f; break;
        case 4:  value = 1; accelScale = 8192.0f; break;
        case 8:  value = 2; accelScale = 4096.0f; break;
        default: value = 3; accelScale = 2048.0f; break;  // 16g
    }
    writeRegister(0x1C, value << 3);  // ACCEL_CONFIG
}

void MPU6050Driver::setGyroRange(uint16_t range) {
    uint8_t value;
    switch (range) {
        case 250:  value = 0; gyroScale = 131.0f; break;
        case 500:  value = 1; gyroScale = 65.5f; break;
        case 1000: value = 2; gyroScale = 32.8f; break;
        default:   value = 3; gyroScale = 16.4f; break;  // 2000
    }
    writeRegister(0x1B, value << 3);  // GYRO_CONFIG
}

void MPU6050Driver::setDLPF(uint8_t bandwidth) {
    writeRegister(0x1A, bandwidth & 0x07);  // CONFIG
}

void MPU6050Driver::enableMotionInterrupt(bool enable, float threshold) {
    if (enable) {
        // Set motion threshold (1mg per LSB at 2g)
        uint8_t thresh = (uint8_t)(threshold * 255);
        writeRegister(0x1F, thresh);  // MOT_THR
        
        // Set duration
        writeRegister(0x20, 0x01);    // MOT_DUR
        
        // Enable motion interrupt
        writeRegister(0x69, 0x30);     // ACCEL_CONFIG2
        writeRegister(0x38, 0x40);    // INT_ENABLE: motion enable
    } else {
        writeRegister(0x38, 0x01);    // INT_ENABLE: data ready only
    }
}

uint8_t MPU6050Driver::getInterruptSource() {
    return readRegister(0x3A);  // INT_STATUS
}

void MPU6050Driver::calibrate(float& axOff, float& ayOff, float& azOff) {
    float sumX = 0, sumY = 0, sumZ = 0;
    const int samples = 100;
    
    for (int i = 0; i < samples; i++) {
        float ax, ay, az;
        readAccel(ax, ay, az);
        sumX += ax;
        sumY += ay;
        sumZ += az;
        delay(10);
    }
    
    accelOffX = sumX / samples;
    accelOffY = sumY / samples;
    accelOffZ = (sumZ / samples) - 1.0f;  // Z should be 1g when flat
    
    axOff = accelOffX;
    ayOff = accelOffY;
    azOff = accelOffZ;
}

void MPU6050Driver::getOffsets(float& axOff, float& ayOff, float& azOff) {
    axOff = accelOffX;
    ayOff = accelOffY;
    azOff = accelOffZ;
}

void MPU6050Driver::setOffsets(float axOff, float ayOff, float azOff) {
    accelOffX = axOff;
    accelOffY = ayOff;
    accelOffZ = azOff;
}

bool MPU6050Driver::testConnection() {
    return readRegister(0x75) == 0x68;
}

void MPU6050Driver::writeRegister(uint8_t reg, uint8_t value) {
    i2cLock();
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
    i2cUnlock();
}

uint8_t MPU6050Driver::readRegister(uint8_t reg) {
    i2cLock();
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, 1);
    uint8_t result = Wire.read();
    i2cUnlock();
    return result;
}