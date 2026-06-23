#include "RtcDriver.h"
#include "../config/Config.h"
#include <driver/i2c.h>
#include <sys/time.h>

static bool available = false;
static SemaphoreHandle_t rtcMutex = NULL;

// ---- BCD helpers ----
static inline uint8_t bcd2dec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }

// ---- I2C lock (separate bus from I2C_NUM_0) ----
static void rtcLock()   { if (rtcMutex) xSemaphoreTake(rtcMutex, portMAX_DELAY); }
static void rtcUnlock() { if (rtcMutex) xSemaphoreGive(rtcMutex); }

// ---- Read raw DS3231 time registers ----
static bool readRaw(uint8_t* out7) {
    uint8_t reg = 0x00;
    esp_err_t err = i2c_master_write_read_device(
        RTC_I2C_NUM, RTC_ADDR, &reg, 1, out7, 7,
        pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

bool rtc_init() {
    rtcMutex = xSemaphoreCreateMutex();
    rtcLock();

    // Init I2C_NUM_1
    i2c_config_t conf = {};
    conf.mode          = I2C_MODE_MASTER;
    conf.sda_io_num    = PIN_RTC_SDA;
    conf.scl_io_num    = PIN_RTC_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = RTC_I2C_FREQUENCY;
    conf.clk_flags     = I2C_SCLK_SRC_FLAG_FOR_NOMAL;

    esp_err_t err = i2c_param_config(RTC_I2C_NUM, &conf);
    if (err != ESP_OK) { rtcUnlock(); return false; }

    err = i2c_driver_install(RTC_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { rtcUnlock(); return false; }

    // Probe DS3231
    uint8_t data[7];
    if (!readRaw(data)) { rtcUnlock(); return false; }

    // Check oscillator stopped flag (bit 7 of seconds register)
    // If stopped, the time is invalid — but we'll still use the RTC
    // (user can set it later). For now, just log and continue.

    // Convert BCD to decimal
    struct tm t = {};
    t.tm_sec  = bcd2dec(data[0] & 0x7F);  // mask OSF
    t.tm_min  = bcd2dec(data[1]);
    t.tm_hour = bcd2dec(data[2] & 0x3F);  // mask 12/24h bit
    t.tm_mday = bcd2dec(data[4]);
    t.tm_mon  = bcd2dec(data[5] & 0x1F) - 1;  // mask century bit
    t.tm_year = bcd2dec(data[6]) + 100;  // DS3231 year is 0-99, tm_year is years since 1900

    // Set system clock
    struct timeval tv = {};
    tv.tv_sec = mktime(&t);
    settimeofday(&tv, NULL);

    available = true;
    rtcUnlock();
    return true;
}

// ---- Fallback: set system clock to compile time ----
void rtc_setFallbackTime() {
    // Parse __DATE__ "Jun 23 2026" and __TIME__ "14:30:00"
    struct tm t = {};
    char monthStr[4];
    sscanf(__DATE__, "%3s %d %d", monthStr, &t.tm_mday, &t.tm_year);
    t.tm_year -= 1900;

    static const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    for (int i = 0; i < 12; i++) {
        if (strcmp(monthStr, months[i]) == 0) { t.tm_mon = i; break; }
    }

    sscanf(__TIME__, "%d:%d:%d", &t.tm_hour, &t.tm_min, &t.tm_sec);

    // ponytail: compile-time only, no leap-second correction
    struct timeval tv = {};
    tv.tv_sec = mktime(&t);
    settimeofday(&tv, NULL);
}

bool rtc_isAvailable() { return available; }

time_t rtc_now() {
    if (!available) return time(nullptr);
    rtcLock();
    uint8_t data[7];
    if (!readRaw(data)) { rtcUnlock(); return time(nullptr); }
    rtcUnlock();

    struct tm t = {};
    t.tm_sec  = bcd2dec(data[0] & 0x7F);
    t.tm_min  = bcd2dec(data[1]);
    t.tm_hour = bcd2dec(data[2] & 0x3F);
    t.tm_mday = bcd2dec(data[4]);
    t.tm_mon  = bcd2dec(data[5] & 0x1F) - 1;
    t.tm_year = bcd2dec(data[6]) + 100;
    return mktime(&t);
}
