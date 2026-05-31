#ifndef JSON_BUILDER_H
#define JSON_BUILDER_H

#include <Arduino.h>

// Fixed-buffer JSON serializer. Zero heap allocation.
// Buffer is stack-friendly (2KB) — enough for all API responses.
// If buffer overflows, ok() returns false and output is truncated.

class JsonBuilder {
public:
    JsonBuilder() : pos(0), _overflow(false), _needComma(false), _inArray(false) {
        buf[0] = '\0';
    }

    void clear() {
        pos = 0;
        _overflow = false;
        _needComma = false;
        _inArray = false;
        buf[0] = '\0';
    }

    // --- Object ---
    void startObj() {
        comma();
        append('{');
        _needComma = false;
    }

    void endObj() {
        append('}');
        _needComma = true;
    }

    // --- Key-value pairs ---
    void addStr(const char* key, const char* val) {
        writeKey(key);
        append('"');
        escapeStr(val);
        append('"');
        _needComma = true;
    }

    void addInt(const char* key, int val) {
        writeKey(key);
        itoaWrite(val);
        _needComma = true;
    }

    void addFlt(const char* key, float val, int decimals = 1) {
        writeKey(key);
        ftosWrite(val, decimals);
        _needComma = true;
    }

    void addBool(const char* key, bool val) {
        writeKey(key);
        if (val) {
            append('t'); append('r'); append('u'); append('e');
        } else {
            append('f'); append('a'); append('l'); append('s'); append('e');
        }
        _needComma = true;
    }

    // --- Array ---
    void startArr(const char* key) {
        writeKey(key);
        append('[');
        _needComma = false;
        _inArray = true;
    }

    void endArr() {
        append(']');
        _needComma = true;
        _inArray = false;
    }

    void addArrStr(const char* val) {
        arrComma();
        append('"');
        escapeStr(val);
        append('"');
    }

    void addArrInt(int val) {
        arrComma();
        itoaWrite(val);
    }

    void addArrFlt(float val, int decimals = 1) {
        arrComma();
        ftosWrite(val, decimals);
    }

    // Nested object inside array — start with startArrObj, add fields, end with endObj
    void startArrObj() {
        arrComma();
        append('{');
        _needComma = false;
    }

    void appendRaw(const char* s) {
        if (!s || _overflow) return;
        while (*s && pos < BUF_SIZE - 1) buf[pos++] = *s++;
        buf[pos] = '\0';
    }

    void appendRaw(const String& s) {
        appendRaw(s.c_str());
    }

    const char* str() {
        return buf;
    }

    bool ok() const {
        return !_overflow;
    }

    size_t len() const {
        return pos;
    }

private:
    static const size_t BUF_SIZE = 2048;
    char buf[BUF_SIZE];
    size_t pos;
    bool _overflow;
    bool _needComma;
    bool _inArray;

    void append(char c) {
        if (_overflow) return;
        if (pos < BUF_SIZE - 1) {
            buf[pos++] = c;
            buf[pos] = '\0';
        } else {
            _overflow = true;
        }
    }

    void comma() {
        if (_needComma) append(',');
    }

    void arrComma() {
        if (_needComma) append(',');
        _needComma = true;
    }

    void writeKey(const char* key) {
        comma();
        append('"');
        escapeStr(key);
        append('"');
        append(':');
    }

    void escapeStr(const char* s) {
        if (!s) return;
        while (*s) {
            char c = *s++;
            switch (c) {
                case '"':  append('\\'); append('"'); break;
                case '\\': append('\\'); append('\\'); break;
                case '\n': append('\\'); append('n'); break;
                case '\r': append('\\'); append('r'); break;
                case '\t': append('\\'); append('t'); break;
                default:   append(c); break;
            }
        }
    }

    void itoaWrite(int val) {
        if (val < 0) { append('-'); val = -val; }
        if (val == 0) { append('0'); return; }
        char tmp[12];
        int i = 0;
        while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
        while (i > 0) append(tmp[--i]);
    }

    void ftosWrite(float val, int decimals) {
        if (val < 0) { append('-'); val = -val; }
        // Integer part
        int ipart = (int)val;
        itoaWrite(ipart);
        append('.');
        // Fractional part
        float frac = val - ipart;
        for (int i = 0; i < decimals; i++) {
            frac *= 10;
            int d = (int)frac;
            append('0' + d);
            frac -= d;
        }
    }
};

#endif // JSON_BUILDER_H
