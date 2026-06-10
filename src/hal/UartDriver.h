#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>
#include <stddef.h>

// ESP-IDF UART HAL — replaces Arduino HardwareSerial for sensor UARTs.
// Used by FingerprintDriver (UART2: RX=5, TX=4, 57600 baud).

void  uartBegin(int uartNum, int rxPin, int txPin, int baud, int rxBuf = 256, int txBuf = 256);
void  uartEnd(int uartNum);
int   uartAvailable(int uartNum);
int   uartRead(int uartNum);
void  uartWrite(int uartNum, uint8_t byte);
void  uartWriteBuf(int uartNum, const uint8_t* data, size_t len);
void  uartFlush(int uartNum);
void  uartSetBaud(int uartNum, int baud);

#endif // UART_DRIVER_H
