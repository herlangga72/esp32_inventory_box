#ifndef MIN_STRING_H
#define MIN_STRING_H

#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Minimal fixed-buffer String — replaces Arduino String for JSON, logging, config.
// No heap allocation. 256-byte stack buffer. C++98-compatible.
// Only implements methods actually used across the codebase.

class String {
    char _buf[256];

public:
    String()                        { _buf[0] = '\0'; }
    String(const char* s)           { set(s); }
    String(const String& o)         { set(o._buf); }

    // Operators
    String& operator=(const char* s)    { set(s); return *this; }
    String& operator=(const String& o)  { set(o._buf); return *this; }

    bool operator==(const char* s) const { return strcmp(_buf, s ? s : "") == 0; }
    bool operator!=(const char* s) const { return !(*this == s); }

    // Concatenation (non-modifying — used in JsonParser error messages)
    friend String operator+(const String& a, const char* b) {
        String r(a);
        r.append(b);
        return r;
    }
    friend String operator+(const String& a, char c) {
        String r(a);
        size_t len = strlen(r._buf);
        if (len < sizeof(r._buf) - 2) { r._buf[len] = c; r._buf[len + 1] = '\0'; }
        return r;
    }

    // Access
    const char* c_str() const   { return _buf; }
    size_t      length() const  { return strlen(_buf); }

    // Modification
    void clear()                { _buf[0] = '\0'; }
    void reserve(int)           {}   // no-op (buffer is fixed)
    void concat(const char* s)  { append(s); }

    String& operator+=(const char* s)  { append(s); return *this; }
    String& operator+=(char c) {
        size_t len = strlen(_buf);
        if (len < sizeof(_buf) - 2) { _buf[len] = c; _buf[len + 1] = '\0'; }
        return *this;
    }

    // Substring
    String substring(int start, int end) const {
        String r;
        int len = (int)strlen(_buf);
        if (start < 0) start = 0;
        if (end > len) end = len;
        if (start >= end) return r;
        int n = end - start;
        if (n > (int)sizeof(r._buf) - 1) n = sizeof(r._buf) - 1;
        memcpy(r._buf, _buf + start, n);
        r._buf[n] = '\0';
        return r;
    }

    // Search
    int indexOf(char c, int from = 0) const {
        if (from < 0) from = 0;
        for (size_t i = from; _buf[i]; i++) {
            if (_buf[i] == c) return (int)i;
        }
        return -1;
    }

    // Conversion
    int toInt() const { return atoi(_buf); }
    float toFloat() const { return (float)atof(_buf); }

    // Equality for String objects
    bool equals(const String& o) const { return strcmp(_buf, o._buf) == 0; }

private:
    void set(const char* s) {
        if (s) { strncpy(_buf, s, sizeof(_buf) - 1); _buf[sizeof(_buf) - 1] = '\0'; }
        else _buf[0] = '\0';
    }
    void append(const char* s) {
        if (!s) return;
        size_t cur = strlen(_buf);
        strncpy(_buf + cur, s, sizeof(_buf) - cur - 1);
        _buf[sizeof(_buf) - 1] = '\0';
    }
};

#endif // MIN_STRING_H
