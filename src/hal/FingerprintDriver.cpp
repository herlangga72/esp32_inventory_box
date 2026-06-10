#include "FingerprintDriver.h"
#include "../config/Config.h"
#include "../kernel/ServiceRegistry.h"
#include "../utils/LogManager.h"
#include "TimerDriver.h"

FingerprintDriver::FingerprintDriver()
    : fpSerial(nullptr), finger(nullptr), operational(false),
      enrolling(false), enrollingId(0), firstImageDone(false),
      enrollStartMs(0), scanning(false) {}

bool FingerprintDriver::begin() {
    void* pool = g_registry.getHALPool(ServiceId::FINGERPRINT);
    fpSerial = new (pool) HardwareSerial(2);
    fpSerial->begin(FP_BAUDRATE, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);

    finger = new ((uint8_t*)pool + sizeof(HardwareSerial)) Adafruit_Fingerprint(fpSerial);

    if (!finger->verifyPassword()) {
        LOG_WARN("FINGERP", "Sensor not responding on RX=%d TX=%d baud=%d",
                 PIN_FP_RX, PIN_FP_TX, FP_BAUDRATE);
        operational = false;
        return false;
    }

    operational = true;
    int count = finger->getTemplateCount();
    LOG_INFO("FINGERP", "Sensor ready. Stored templates: %d", count);
    return true;
}

bool FingerprintDriver::isOperational() const { return operational; }

void FingerprintDriver::end() {
    if (finger) { finger = nullptr; }
    if (fpSerial) {
        fpSerial->end();
        fpSerial = nullptr;
    }
    operational = false; scanning = false; enrolling = false;
}

void FingerprintDriver::startScan() { scanning = true; }

int FingerprintDriver::checkScan() {
    if (!operational || !scanning) return -1;
    int result = finger->getImage();
    switch (result) {
    case FINGERPRINT_OK:
        result = finger->image2Tz(1);
        if (result != FINGERPRINT_OK) return -2;
        break;
    case FINGERPRINT_NOFINGER: return -1;
    default: return -1;
    }
    result = finger->fingerSearch();
    if (result == FINGERPRINT_OK) {
        LOG_INFO("FINGERP","SCAN match=%d confidence=%d", finger->fingerID, finger->confidence);
        return finger->fingerID;
    }
    return (result == FINGERPRINT_NOTFOUND) ? -2 : -1;
}

bool FingerprintDriver::startEnroll(int id) {
    if (!operational || id < 1 || id > Config::MAX_FINGERPRINTS) return false;
    enrolling = true; enrollingId = id; firstImageDone = false;
    enrollStartMs = tmrMillis();
    LOG_INFO("FINGERP","ENROLL_START id=%d", id);
    return true;
}

int FingerprintDriver::checkEnrollStep() {
    if (!operational || !enrolling) return -1;
    if (tmrMillis() - enrollStartMs > (unsigned long)ENROLL_TIMEOUT_MS) {
        LOG_WARN("FINGERP","ENROLL_TIMEOUT id=%d", enrollingId);
        enrolling = false; return -1;
    }
    if (!firstImageDone) {
        int r = finger->getImage();
        if (r == FINGERPRINT_NOFINGER) return 0;
        if (r != FINGERPRINT_OK) return -2;
        r = finger->image2Tz(1);
        if (r != FINGERPRINT_OK) return -2;
        firstImageDone = true;
        LOG_INFO("FINGERP","ENROLL_STEP1 id=%d", enrollingId);
        return 1;
    }
    int r = finger->getImage();
    if (r == FINGERPRINT_NOFINGER) return 1;
    if (r != FINGERPRINT_OK) return -2;
    r = finger->image2Tz(2);
    if (r != FINGERPRINT_OK) return -2;
    r = finger->createModel();
    if (r != FINGERPRINT_OK) return -2;
    r = finger->storeModel(enrollingId);
    if (r != FINGERPRINT_OK) return -2;
    enrolling = false;
    LOG_INFO("FINGERP","ENROLL_COMPLETE id=%d", enrollingId);
    return 2;
}

void FingerprintDriver::cancelEnroll() {
    enrolling = false;
    LOG_INFO("FINGERP","ENROLL_CANCELLED id=%d", enrollingId);
}

bool FingerprintDriver::deleteFingerprint(int id) {
    if (!operational) return false;
    int r = finger->deleteModel(id);
    if (r == FINGERPRINT_OK) { LOG_INFO("FINGERP","DELETE id=%d", id); return true; }
    LOG_WARN("FINGERP","DELETE_FAIL id=%d code=%d", id, r);
    return false;
}

bool FingerprintDriver::deleteAll() {
    if (!operational) return false;
    int r = finger->emptyDatabase();
    if (r == FINGERPRINT_OK) { LOG_INFO("FINGERP","DELETE_ALL"); return true; }
    LOG_WARN("FINGERP","DELETE_ALL_FAIL code=%d", r);
    return false;
}

int FingerprintDriver::getTemplateCount() {
    return operational ? finger->getTemplateCount() : -1;
}

uint32_t FingerprintDriver::getSensorID() {
    return operational ? (uint32_t)finger->templateCount : 0;
}
