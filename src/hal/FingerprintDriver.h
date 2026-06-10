#ifndef FINGERPRINT_DRIVER_H
#define FINGERPRINT_DRIVER_H

#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>

class FingerprintDriver {
public:
    FingerprintDriver();

    bool begin();
    bool isOperational() const;
    void end();

    void startScan();
    int  checkScan();

    bool startEnroll(int id);
    int  checkEnrollStep();
    void cancelEnroll();

    bool deleteFingerprint(int id);
    bool deleteAll();
    int  getTemplateCount();
    uint32_t getSensorID();

private:
    HardwareSerial* fpSerial;
    Adafruit_Fingerprint* finger;
    bool operational;

    bool enrolling;
    int  enrollingId;
    bool firstImageDone;
    unsigned long enrollStartMs;

    bool scanning;
    static const int ENROLL_TIMEOUT_MS = 30000;
};

#endif // FINGERPRINT_DRIVER_H
