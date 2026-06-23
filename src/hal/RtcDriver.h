#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include <Arduino.h>
#include <time.h>

// DS3231 RTC over I2C_NUM_1. Falls back to compile-time + uptime if absent.

bool rtc_init();
bool rtc_isAvailable();
time_t rtc_now();
void rtc_setFallbackTime();   // compile-time + uptime when RTC absent

#endif // RTC_DRIVER_H
