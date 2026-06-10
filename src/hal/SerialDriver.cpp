#include "SerialDriver.h"

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#define UART_NUM    UART_NUM_0
#define TX_PIN      1
#define RX_PIN      3
#define BUF_SIZE    256

static bool initialized = false;
static SemaphoreHandle_t uartMutex = NULL;

void serBegin(unsigned long baud) {
    if (initialized) return;

    uart_config_t cfg = {};
    cfg.baud_rate  = (int)baud;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_APB;

    uart_param_config(UART_NUM, &cfg);
    uart_set_pin(UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0);

    uartMutex = xSemaphoreCreateMutex();
    initialized = true;
}

bool serReady() { return initialized; }

void serPrint(const char* str) {
    if (!initialized || !str) return;
    xSemaphoreTake(uartMutex, portMAX_DELAY);
    uart_write_bytes(UART_NUM, str, strlen(str));
    xSemaphoreGive(uartMutex);
}

void serPrintln(const char* str) {
    if (!initialized || !str) return;
    xSemaphoreTake(uartMutex, portMAX_DELAY);
    uart_write_bytes(UART_NUM, str, strlen(str));
    uart_write_bytes(UART_NUM, "\r\n", 2);
    xSemaphoreGive(uartMutex);
}

void serPrintf(const char* fmt, ...) {
    if (!initialized || !fmt) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        xSemaphoreTake(uartMutex, portMAX_DELAY);
        uart_write_bytes(UART_NUM, buf, len);
        xSemaphoreGive(uartMutex);
    }
}

int serAvailable() {
    if (!initialized) return 0;
    size_t avail = 0;
    uart_get_buffered_data_len(UART_NUM, &avail);
    return (int)avail;
}

int serRead() {
    if (!initialized) return -1;
    uint8_t byte;
    int rc = uart_read_bytes(UART_NUM, &byte, 1, 0);
    return (rc > 0) ? (int)byte : -1;
}

void serFlush() {
    if (!initialized) return;
    uart_flush(UART_NUM);
}
