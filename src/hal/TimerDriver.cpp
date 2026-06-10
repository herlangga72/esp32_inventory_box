#include "TimerDriver.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

void tmrDelay(unsigned long ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void tmrYield() {
    taskYIELD();
}

unsigned long tmrMillis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

unsigned long tmrMicros() {
    return (unsigned long)esp_timer_get_time();
}
