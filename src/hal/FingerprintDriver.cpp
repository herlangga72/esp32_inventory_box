#include "FingerprintDriver.h"
#include "../config/Config.h"
#include "../utils/LogManager.h"

FingerprintDriver::FingerprintDriver()
    : fpSerial(nullptr), finger(nullptr), operational(false),
      enrolling(false), enrollingId(0), firstImageDone(false),
      enrollStartMs(0), scanning(false) {}

bool FingerprintDriver::begin() {
    // Serial2 with remapped pins
    fpSerial = new HardwareSerial(2);
    if (!fpSerial) {
        LOG_ERROR("FINGERP", "OOM: HardwareSerial allocation failed");
        operational = false;
        return false;
    }
    fpSerial->begin(FP_BAUDRATE, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);

    // Create Adafruit_Fingerprint with our serial port
    finger = new Adafruit_Fingerprint(fpSerial);
    if (!finger) {
        LOG_ERROR("FINGERP", "OOM: Adafruit_Fingerprint allocation failed");
        delete fpSerial;
        fpSerial = nullptr;
        operational = false;
        return false;
    }

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

bool FingerprintDriver::isOperational() const {
    return operational;
}

void FingerprintDriver::end() {
    if (finger) {
        delete finger;
        finger = nullptr;
    }
    if (fpSerial) {
        fpSerial->end();
        delete fpSerial;
        fpSerial = nullptr;
    }
    operational = false;
    scanning = false;
    enrolling = false;
}

// ---- Scanning ----

void FingerprintDriver::startScan() {
    scanning = true;
}

int FingerprintDriver::checkScan() {
    if (!operational || !scanning) return -1;

    int result = finger->getImage();

    switch (result) {
    case FINGERPRINT_OK:
        // Image captured, convert to template
        result = finger->image2Tz(1);
        if (result != FINGERPRINT_OK) return -2;
        break;
    case FINGERPRINT_NOFINGER:
        return -1;
    case FINGERPRINT_PACKETRECIEVEERR:
    case FINGERPRINT_IMAGEFAIL:
    case FINGERPRINT_TIMEOUT:
    default:
        return -1;  // transient, try again
    }

    // Image captured and converted — now search against stored templates
    result = finger->fingerSearch();

    if (result == FINGERPRINT_OK) {
        // Match found
        int id = finger->fingerID;
        int confidence = finger->confidence;
        LOG_INFO("FINGERP","SCAN match=%d confidence=%d", id, confidence);
        return id;
    }

    if (result == FINGERPRINT_NOTFOUND) {
        LOG_INFO("FINGERP","SCAN no match");
        return -2;  // scanned but no match
    }

    return -1;  // error, try again
}

// ---- Enrollment ----

bool FingerprintDriver::startEnroll(int id) {
    if (!operational) return false;
    if (id < 1 || id > Config::MAX_FINGERPRINTS) return false;

    enrolling = true;
    enrollingId = id;
    firstImageDone = false;
    enrollStartMs = millis();

    LOG_INFO("FINGERP","ENROLL_START id=%d", id);
    return true;
}

int FingerprintDriver::checkEnrollStep() {
    if (!operational || !enrolling) return -1;

    // Timeout check
    if (millis() - enrollStartMs > (unsigned long)ENROLL_TIMEOUT_MS) {
        LOG_WARN("FINGERP","ENROLL_TIMEOUT id=%d", enrollingId);
        enrolling = false;
        return -1;
    }

    if (!firstImageDone) {
        // Step 1: capture first image
        int result = finger->getImage();
        if (result == FINGERPRINT_NOFINGER) return 0;
        if (result != FINGERPRINT_OK) return -2;

        result = finger->image2Tz(1);
        if (result != FINGERPRINT_OK) return -2;

        firstImageDone = true;
        LOG_INFO("FINGERP","ENROLL_STEP1 id=%d", enrollingId);
        return 1;  // need second image
    }

    // Step 2: capture second image
    int result = finger->getImage();
    if (result == FINGERPRINT_NOFINGER) return 1;
    if (result != FINGERPRINT_OK) return -2;

    result = finger->image2Tz(2);
    if (result != FINGERPRINT_OK) return -2;

    // Create model from two images
    result = finger->createModel();
    if (result != FINGERPRINT_OK) return -2;

    // Store model at ID
    result = finger->storeModel(enrollingId);
    if (result != FINGERPRINT_OK) return -2;

    enrolling = false;
    LOG_INFO("FINGERP","ENROLL_COMPLETE id=%d", enrollingId);
    return 2;  // success
}

void FingerprintDriver::cancelEnroll() {
    enrolling = false;
    LOG_INFO("FINGERP","ENROLL_CANCELLED id=%d", enrollingId);
}

// ---- Management ----

bool FingerprintDriver::deleteFingerprint(int id) {
    if (!operational) return false;

    int result = finger->deleteModel(id);
    if (result == FINGERPRINT_OK) {
        LOG_INFO("FINGERP","DELETE id=%d", id);
        return true;
    }
    LOG_WARN("FINGERP","DELETE_FAIL id=%d code=%d", id, result);
    return false;
}

bool FingerprintDriver::deleteAll() {
    if (!operational) return false;

    int result = finger->emptyDatabase();
    if (result == FINGERPRINT_OK) {
        LOG_INFO("FINGERP","DELETE_ALL");
        return true;
    }
    LOG_WARN("FINGERP","DELETE_ALL_FAIL code=%d", result);
    return false;
}

int FingerprintDriver::getTemplateCount() {
    if (!operational) return -1;
    return finger->getTemplateCount();
}

uint32_t FingerprintDriver::getSensorID() {
    if (!operational) return 0;
    return (uint32_t)finger->templateCount;
}
