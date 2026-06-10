#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include <stdint.h>
#include <stddef.h>

#ifndef MPU6050_ADDR
#define MPU6050_ADDR 0x68
#endif
#ifndef PIN_MPU_SDA
#define PIN_MPU_SDA 21
#endif
#ifndef PIN_MPU_SCL
#define PIN_MPU_SCL 22
#endif

class MPU6050Driver {
public:
    MPU6050Driver(uint8_t addr = MPU6050_ADDR);

    bool begin();
    void readAccel(float& ax, float& ay, float& az);
    void readGyro(float& gx, float& gy, float& gz);
    void readMotion6(float& ax, float& ay, float& az,
                     float& gx, float& gy, float& gz);

    void setAccelRange(uint8_t range);
    void setGyroRange(uint16_t range);
    void setDLPF(uint8_t bandwidth);

    void enableMotionInterrupt(bool enable, float threshold = 0.5f);
    uint8_t getInterruptSource();

    void calibrate(float& axOff, float& ayOff, float& azOff);
    void getOffsets(float& axOff, float& ayOff, float& azOff);
    void setOffsets(float axOff, float ayOff, float azOff);

    bool testConnection();

private:
    uint8_t addr;
    float accelOffX, accelOffY, accelOffZ;
    float accelScale, gyroScale;

    // Internal unlocked I/O (caller holds i2cLock)
    void _writeReg(uint8_t reg, uint8_t value);
    uint8_t _readReg(uint8_t reg);
    void _readBytes(uint8_t reg, uint8_t* buf, size_t len);

    // Public locked wrappers
    uint8_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t value);
};

#endif // MPU6050_DRIVER_H
