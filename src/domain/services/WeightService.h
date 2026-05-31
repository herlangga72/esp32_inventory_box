#ifndef WEIGHT_SERVICE_H
#define WEIGHT_SERVICE_H

#include <Arduino.h>
#include "../hal/HX711Driver.h"
#include "../events/EventBus.h"
#include "config/Config.h"

class WeightService {
public:
    WeightService(HX711Driver* driver);
    
    void begin();
    void update();
    
    float getCurrentWeight();
    float getBaseline();
    float getDelta();
    void setBaseline(float baseline);
    
    void startCalibration(int samples = 50);
    bool isCalibrating();
    bool isCalibrationComplete();
    float getCalibrationResult();
    
    void onRawReading(int32_t raw);
    void tare();
    
    // Config
    void setCalibrationFactor(float factor);
    float getCalibrationFactor();

private:
    HX711Driver* hx711;
    
    // Filtering
    static const uint8_t FILTER_SIZE = Config::FILTER_SIZE;
    float readings[FILTER_SIZE];
    uint8_t filterIndex;
    float filterSum;
    
    float baseline;
    float currentWeight;
    float previousWeight;
    float calibrationFactor;
    
    // Calibration
    bool calibrating;
    int calibrationSamples;
    int totalCalSamples;
    float calibrationSum;
    
    float applyMovingAverage(int32_t raw);
    void processWeight();
};

#endif // WEIGHT_SERVICE_H