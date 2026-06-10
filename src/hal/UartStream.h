#ifndef UART_STREAM_H
#define UART_STREAM_H

#include <stdint.h>
#include <stddef.h>

// Minimal Stream-like interface for Adafruit_Fingerprint library.
// Wraps UartDriver (ESP-IDF) instead of Arduino HardwareSerial.
// Only implements methods the fingerprint library actually calls.

class UartStream {
public:
    UartStream() : uartNum(-1) {}

    void begin(int baud, int config, int rxPin, int txPin);
    void end();

    int  available();
    int  read();
    int  peek();
    void flush();

    // Stream::write — fingerprint lib sends command packets
    size_t write(uint8_t byte);
    size_t write(const uint8_t* buf, size_t len);

    // Required by Adafruit_Fingerprint (it checks if serial is truthy)
    operator bool() const { return uartNum >= 0; }

private:
    int uartNum;
};

#endif // UART_STREAM_H
