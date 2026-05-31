#ifndef FINGERPRINT_DRIVER_H
#define FINGERPRINT_DRIVER_H

#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>

class FingerprintDriver {
public:
    FingerprintDriver();

    // Lifecycle
    bool begin();
    bool isOperational() const;
    void end();

    // Scanning (non-blocking polling model)
    void startScan();           // arm sensor to look for a finger
    int  checkScan();           // returns -1 (no finger), -2 (fail), >=0 (finger ID)

    // Enrollment (multi-step)
    bool startEnroll(int id);   // begin enrollment for given ID slot
    int  checkEnrollStep();     // returns -2 (fail), 0 (need image1), 1 (need image2),
                                //   2 (success), -1 (timeout/no action)
    void cancelEnroll();

    // Management
    bool deleteFingerprint(int id);
    bool deleteAll();
    int  getTemplateCount();

    // Sensor info
    uint32_t getSensorID();

private:
    HardwareSerial* fpSerial;  // Serial2 with custom pins
    Adafruit_Fingerprint* finger;  // created after serial setup
    bool operational;

    // Enrollment state
    bool enrolling;
    int  enrollingId;
    bool firstImageDone;
    unsigned long enrollStartMs;

    // Scan state
    bool scanning;

    static const int ENROLL_TIMEOUT_MS = 30000;
};

#endif // FINGERPRINT_DRIVER_H
