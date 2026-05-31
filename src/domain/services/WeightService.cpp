#include "WeightService.h"
#include "../events/EventBus.h"
#include "config/Config.h"

WeightService::WeightService(HX711Driver* driver)
    : hx711(driver), filterIndex(0), filterSum(0),
      baseline(0), currentWeight(0), previousWeight(0),
      calibrationFactor(Config::CALIBRATION_FACTOR),
      calibrating(false), calibrationSamples(0), totalCalSamples(0), calibrationSum(0) {
    memset(readings, 0, sizeof(readings));
}

void WeightService::begin() {
    hx711->begin();
}

void WeightService::update() {
    if (hx711->isReady()) {
        int32_t raw = hx711->readRaw();
        onRawReading(raw);
    }
}

float WeightService::applyMovingAverage(int32_t raw) {
    // Subtract old value from sum
    filterSum -= readings[filterIndex];
    
    // Add new reading
    readings[filterIndex] = raw * calibrationFactor;
    filterSum += readings[filterIndex];
    
    // Advance index
    filterIndex = (filterIndex + 1) % Config::FILTER_SIZE;
    
    // Return average
    return filterSum / Config::FILTER_SIZE;
}

void WeightService::onRawReading(int32_t raw) {
    previousWeight = currentWeight;
    currentWeight = applyMovingAverage(raw);

    // Calibration mode: accumulate samples until the requested count is reached
    if (calibrating && calibrationSamples > 0) {
        calibrationSum += currentWeight;
        calibrationSamples--;
        if (calibrationSamples == 0) {
            baseline = calibrationSum / totalCalSamples;
            calibrating = false;

            // Publish calibration complete event
            EventPayload event;
            event.type = DomainEvent::CALIBRATION_COMPLETE;
            event.timestamp = millis();
            event.data.generic.value = baseline;
            EventBus::getInstance()->publish(event);
        }
        return;  // don't publish WEIGHT_UPDATED during calibration
    }

    processWeight();
}

void WeightService::processWeight() {
    float delta = currentWeight - baseline;
    
    EventPayload event;
    event.type = DomainEvent::WEIGHT_UPDATED;
    event.timestamp = millis();
    event.data.weight.weight = currentWeight;
    event.data.weight.delta = delta;
    event.data.weight.baseline = baseline;
    
    EventBus::getInstance()->publish(event);
    
    // Check for significant change
    if (abs(delta) > Config::WEIGHT_THRESHOLD_GRAMS) {
        EventPayload changeEvent;
        changeEvent.type = DomainEvent::WEIGHT_CHANGE_SIGNIFICANT;
        changeEvent.timestamp = millis();
        changeEvent.data.weight.weight = currentWeight;
        changeEvent.data.weight.delta = delta;
        EventBus::getInstance()->publish(changeEvent);
    }
}

float WeightService::getCurrentWeight() {
    return currentWeight;
}

float WeightService::getBaseline() {
    return baseline;
}

float WeightService::getDelta() {
    return currentWeight - baseline;
}

void WeightService::setBaseline(float newBaseline) {
    baseline = newBaseline;
}

void WeightService::startCalibration(int samples) {
    calibrating = true;
    calibrationSamples = samples;
    totalCalSamples = samples;
    calibrationSum = 0;
}

bool WeightService::isCalibrating() {
    return calibrating;
}

bool WeightService::isCalibrationComplete() {
    return !calibrating && totalCalSamples > 0 && calibrationSamples == 0;
}

float WeightService::getCalibrationResult() {
    if (calibrationSum == 0) return baseline;
    int collected = totalCalSamples - calibrationSamples;
    if (collected <= 0) return baseline;
    return calibrationSum / collected;
}

void WeightService::tare() {
    hx711->tare(10);
    baseline = 0;
}

void WeightService::setCalibrationFactor(float factor) {
    calibrationFactor = factor;
}

float WeightService::getCalibrationFactor() {
    return calibrationFactor;
}