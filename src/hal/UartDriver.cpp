#include "UartDriver.h"

#include <driver/uart.h>
#include <cstring>

#define MAX_UARTS 3
static bool installed[MAX_UARTS] = {false};

void uartBegin(int uartNum, int rxPin, int txPin, int baud, int rxBuf, int txBuf) {
    if ((unsigned)uartNum >= MAX_UARTS || installed[uartNum]) return;

    uart_config_t cfg = {};
    cfg.baud_rate  = baud;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_APB;

    uart_param_config((uart_port_t)uartNum, &cfg);
    uart_set_pin((uart_port_t)uartNum, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install((uart_port_t)uartNum, rxBuf, txBuf, 0, NULL, 0);

    installed[uartNum] = true;
}

void uartEnd(int uartNum) {
    if ((unsigned)uartNum >= MAX_UARTS || !installed[uartNum]) return;
    uart_driver_delete((uart_port_t)uartNum);
    installed[uartNum] = false;
}

int uartAvailable(int uartNum) {
    if ((unsigned)uartNum >= MAX_UARTS || !installed[uartNum]) return 0;
    size_t avail = 0;
    uart_get_buffered_data_len((uart_port_t)uartNum, &avail);
    return (int)avail;
}

int uartRead(int uartNum) {
    if ((unsigned)uartNum >= MAX_UARTS || !installed[uartNum]) return -1;
    uint8_t byte;
    int rc = uart_read_bytes((uart_port_t)uartNum, &byte, 1, 0);
    return (rc > 0) ? (int)byte : -1;
}

void uartWrite(int uartNum, uint8_t byte) {
    if ((unsigned)uartNum >= MAX_UARTS || !installed[uartNum]) return;
    uart_write_bytes((uart_port_t)uartNum, &byte, 1);
}

void uartWriteBuf(int uartNum, const uint8_t* data, size_t len) {
    if ((unsigned)uartNum >= MAX_UARTS || !installed[uartNum] || !data || len == 0) return;
    uart_write_bytes((uart_port_t)uartNum, data, len);
}

void uartFlush(int uartNum) {
    if ((unsigned)uartNum >= MAX_UARTS || !installed[uartNum]) return;
    uart_flush((uart_port_t)uartNum);
}

void uartSetBaud(int uartNum, int baud) {
    if ((unsigned)uartNum >= MAX_UARTS || !installed[uartNum]) return;
    uart_set_baudrate((uart_port_t)uartNum, baud);
}
