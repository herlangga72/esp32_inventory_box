#ifndef HX711_DRIVER_H
#define HX711_DRIVER_H

#include <stdint.h>
#include <driver/gpio.h>

#ifndef PIN_HX711_DT
#define PIN_HX711_DT  16
#endif
#ifndef PIN_HX711_SCK
#define PIN_HX711_SCK 17
#endif
#ifndef PIN_HX711_DRDY
#define PIN_HX711_DRDY 36
#endif

class HX711Driver {
public:
    HX711Driver(int dataPin  = PIN_HX711_DT,
                int clockPin = PIN_HX711_SCK,
                int drdyPin  = PIN_HX711_DRDY);

    void begin();

    // Blocking read (waits up to 100ms for data ready)
    int32_t readRaw();

    // Non-blocking read — returns INT32_MIN if no data ready
    int32_t readRawNonBlocking();

    void setGain(int gain);
    int  getGain() const { return gain; }

    void powerDown();
    void powerUp();

    bool isReady() const;
    void tare(int samples = 10);

    int32_t getOffset() const { return offset; }
    void    setOffset(int32_t val) { offset = val; }

    bool isInitialized() const { return initialized; }

private:
    gpio_num_t pinData;
    gpio_num_t pinClock;
    gpio_num_t pinDRDY;
    int  gain;
    int  gainPulses;     // pre-computed: gain/32 (1 for 128, 3 for 64)
    int32_t offset;
    bool initialized;

    // Bit-bang one 24-bit read (caller ensures data is ready)
    int32_t readBits();
};

#endif // HX711_DRIVER_H
