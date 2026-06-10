// Minimal Arduino stub for native (desktop) unit testing.
// Provides just enough for pure-logic components:
//   - JsonBuilder, JsonParser, Tool/User entities, ServiceMessage
#ifndef ARDUINO_STUB_H
#define ARDUINO_STUB_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cstdio>

using std::size_t;
using std::abs;
using std::strncpy;
using std::strncmp;
using std::strcmp;
using std::strlen;
using std::strchr;

// ---- Minimal String class ----
// Supports construction from const char*, c_str(), and concatenation
// with const char* (needed by JsonParser error messages).
class String {
    char _buf[256];
public:
    String() { _buf[0] = '\0'; }
    String(const char* s) {
        if (s) { strncpy(_buf, s, sizeof(_buf) - 1); _buf[sizeof(_buf) - 1] = '\0'; }
        else _buf[0] = '\0';
    }
    String(const String& other) {
        strncpy(_buf, other._buf, sizeof(_buf) - 1);
        _buf[sizeof(_buf) - 1] = '\0';
    }

    const char* c_str() const { return _buf; }

    String& operator=(const char* s) {
        if (s) { strncpy(_buf, s, sizeof(_buf) - 1); _buf[sizeof(_buf) - 1] = '\0'; }
        else _buf[0] = '\0';
        return *this;
    }

    friend String operator+(const String& a, const char* b) {
        String r(a);
        size_t alen = strlen(r._buf);
        size_t remain = sizeof(r._buf) - alen - 1;
        if (b && remain > 0) { strncpy(r._buf + alen, b, remain); r._buf[sizeof(r._buf) - 1] = '\0'; }
        return r;
    }
    friend String operator+(const String& a, char c) {
        String r(a);
        size_t alen = strlen(r._buf);
        if (alen < sizeof(r._buf) - 2) {
            r._buf[alen] = c;
            r._buf[alen + 1] = '\0';
        }
        return r;
    }
};

// Arduino-style types
typedef unsigned long ulong;

#endif
