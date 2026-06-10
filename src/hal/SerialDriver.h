#ifndef SERIAL_DRIVER_H
#define SERIAL_DRIVER_H

#include <stdint.h>
#include <stddef.h>

// ESP-IDF native UART0 for logging/CLI — replaces Arduino Serial.
// UART0 = TX=GPIO1, RX=GPIO3 (ESP32 DevKit default pins)

void  serBegin(unsigned long baud = 115200);
void  serPrint(const char* str);
void  serPrintln(const char* str);
void  serPrintf(const char* fmt, ...);
int   serAvailable();
int   serRead();
void  serFlush();
bool  serReady();  // true if UART is initialized

#endif // SERIAL_DRIVER_H
