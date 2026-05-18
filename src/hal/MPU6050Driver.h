#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include <Arduino.h>
#include <Wire.h>

class MPU6050Driver {
public:
    MPU6050Driver(uint8_t addr = MPU6050_ADDR);
    
    bool begin();
    void readAccel(float& ax, float& ay, float& az);
    void readGyro(float& gx, float& gy, float& gz);
    void readRaw(int16_t& ax, int16_t& ay, int16_t& az, int16_t& gx, int16_t& gy, int16_t& gz);
    
    void setAccelRange(uint8_t range);   // 2, 4, 8, 16
    void setGyroRange(uint8_t range);     // 250, 500, 1000, 2000
    void setDLPF(uint8_t bandwidth);      // 0-6 (260Hz to 5Hz)
    
    void enableMotionInterrupt(bool enable, float threshold = 0.5f);
    uint8_t getInterruptSource();
    
    void calibrate(float& axOff, float& ayOff, float& azOff);
    void getOffsets(float& axOff, float& ayOff, float& azOff);
    void setOffsets(float axOff, float ayOff, float azOff);
    
    bool testConnection();

private:
    uint8_t addr;
    
    // Calibration offsets
    float accelOffX, accelOffY, accelOffZ;
    
    // Scale factors
    float accelScale;  // LSB/g
    float gyroScale;   // LSB/°/s
    
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
};

#endif // MPU6050_DRIVER_H