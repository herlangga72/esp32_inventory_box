#include "UartStream.h"
#include "UartDriver.h"

// Fingerprint sensor uses UART2 on ESP32 (pins 5=RX, 4=TX, 57600 baud)
#define FP_UART 2

void UartStream::begin(int baud, int /*config*/, int rxPin, int txPin) {
    uartNum = FP_UART;
    uartBegin(uartNum, rxPin, txPin, baud, 256, 256);
}

void UartStream::end() {
    if (uartNum >= 0) {
        uartEnd(uartNum);
        uartNum = -1;
    }
}

int UartStream::available() {
    return (uartNum >= 0) ? uartAvailable(uartNum) : 0;
}

int UartStream::read() {
    return (uartNum >= 0) ? uartRead(uartNum) : -1;
}

int UartStream::peek() {
    // UART driver doesn't support peek natively — read and return.
    // Adafruit lib rarely uses peek; this is a best-effort fallback.
    return (uartNum >= 0) ? uartRead(uartNum) : -1;
}

void UartStream::flush() {
    if (uartNum >= 0) uartFlush(uartNum);
}

size_t UartStream::write(uint8_t byte) {
    if (uartNum < 0) return 0;
    uartWrite(uartNum, byte);
    return 1;
}

size_t UartStream::write(const uint8_t* buf, size_t len) {
    if (uartNum < 0 || !buf || len == 0) return 0;
    uartWriteBuf(uartNum, buf, len);
    return len;
}
