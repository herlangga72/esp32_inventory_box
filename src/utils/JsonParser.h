#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <Arduino.h>

// Zero-allocation JSON → struct mapper.
// Define request structs, describe fields with JField array, call jsonParse().
// Unknown keys ignored. Wrong types → 400. Missing required → check .found.

struct JField {
    enum Type { T_STR, T_INT, T_FLT, T_BOOL };

    const char* key;
    Type        type;
    void*       target;   // pointer to char[] (str), int*, float*, bool*
    size_t      maxLen;   // buffer size for strings (ignored for numbers)
    bool        found;    // set=true by parser when key matches
};

// Parse JSON object string into struct fields. Returns false on malformed input.

// ====== Implementation ======

static inline bool jsonIsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline const char* jsonSkip(const char* p) {
    while (*p && jsonIsSpace(*p)) p++;
    return p;
}

static inline JField* jsonFindField(JField* fields, int count, const char* key, size_t keyLen) {
    for (int i = 0; i < count; i++) {
        if (strncmp(fields[i].key, key, keyLen) == 0 && fields[i].key[keyLen] == '\0') {
            return &fields[i];
        }
    }
    return nullptr;
}

static inline const char* jsonReadStr(const char* p, char* buf, size_t maxLen) {
    if (*p != '"') return nullptr;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) return nullptr;
            switch (*p) {
                case '"':  if (i < maxLen - 1) buf[i++] = '"';  break;
                case '\\': if (i < maxLen - 1) buf[i++] = '\\'; break;
                case 'n':  if (i < maxLen - 1) buf[i++] = '\n'; break;
                case 'r':  if (i < maxLen - 1) buf[i++] = '\r'; break;
                case 't':  if (i < maxLen - 1) buf[i++] = '\t'; break;
                default:   return nullptr;
            }
        } else {
            if (i < maxLen - 1) buf[i++] = *p;
        }
        p++;
    }
    if (*p != '"') return nullptr;
    buf[i] = '\0';
    return p + 1;
}

static inline bool jsonIsDigit(char c) { return c >= '0' && c <= '9'; }

static inline const char* jsonReadNum(const char* p, float* fval, int* ival) {
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (!*p || !jsonIsDigit(*p)) return nullptr;
    int i = 0;
    while (jsonIsDigit(*p)) { i = i * 10 + (*p - '0'); p++; }
    if (*p == '.') {
        p++;
        float frac = 0.0f;
        float div = 10.0f;
        while (jsonIsDigit(*p)) { frac += (*p - '0') / div; div *= 10.0f; p++; }
        *fval = (neg ? -(i + frac) : (i + frac));
        if (ival) *ival = (int)*fval;
    } else {
        *fval = neg ? -i : i;
        if (ival) *ival = neg ? -i : i;
    }
    return p;
}

static inline bool jsonMatch(const char*& p, const char* word) {
    size_t len = strlen(word);
    if (strncmp(p, word, len) != 0) return false;
    p += len;
    return true;
}

static inline bool jsonParse(const char* input, JField* fields, int fieldCount, String& errorMsg) {
    if (!input) {
        errorMsg = "Null input";
        return false;
    }

    const char* p = jsonSkip(input);
    if (!*p || *p != '{') {
        errorMsg = "Expected '{' at start of JSON";
        return false;
    }
    p = jsonSkip(p + 1);

    // Empty object
    if (*p == '}') return true;

    while (*p) {
        p = jsonSkip(p);
        if (!*p) { errorMsg = "Unexpected end of JSON"; return false; }

        // End of object
        if (*p == '}') return true;

        // Read key
        if (*p != '"') { errorMsg = "Expected string key"; return false; }
        const char* keyStart = p + 1;
        const char* keyEnd = strchr(keyStart, '"');
        if (!keyEnd) { errorMsg = "Unterminated string key"; return false; }
        size_t keyLen = keyEnd - keyStart;
        p = keyEnd + 1;

        // Expect ':'
        p = jsonSkip(p);
        if (*p != ':') { errorMsg = "Expected ':' after key"; return false; }
        p = jsonSkip(p + 1);

        JField* f = jsonFindField(fields, fieldCount, keyStart, keyLen);

        // Read value
        if (*p == '"') {
            // String value
            if (f) {
                if (f->type != JField::T_STR) {
                    errorMsg = String("Expected non-string for key '") + f->key + "'";
                    return false;
                }
                size_t max = f->maxLen > 0 ? f->maxLen : 64;
                p = jsonReadStr(p, (char*)f->target, max);
                if (!p) { errorMsg = "Unterminated string value"; return false; }
                f->found = true;
            } else {
                // Skip unknown string
                p++; while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
                if (*p != '"') { errorMsg = "Unterminated string value"; return false; }
                p++;
            }
        } else if (*p == 't' || *p == 'f') {
            // Boolean
            bool val = (*p == 't');
            if (val) {
                if (!jsonMatch(p, "true")) { errorMsg = "Invalid boolean"; return false; }
            } else {
                if (!jsonMatch(p, "false")) { errorMsg = "Invalid boolean"; return false; }
            }
            if (f) {
                if (f->type != JField::T_BOOL) {
                    errorMsg = String("Expected non-bool for key '") + f->key + "'";
                    return false;
                }
                *(bool*)f->target = val;
                f->found = true;
            }
        } else if (*p == '-' || jsonIsDigit(*p)) {
            // Number
            float fval = 0;
            int ival = 0;
            p = jsonReadNum(p, &fval, &ival);
            if (!p) { errorMsg = "Invalid number"; return false; }
            if (f) {
                if (f->type == JField::T_INT) {
                    *(int*)f->target = ival;
                } else if (f->type == JField::T_FLT) {
                    *(float*)f->target = fval;
                } else {
                    errorMsg = String("Expected non-number for key '") + f->key + "'";
                    return false;
                }
                f->found = true;
            }
        } else if (*p == 'n') {
            // null — treat as missing
            if (!jsonMatch(p, "null")) { errorMsg = "Invalid null literal"; return false; }
        } else {
            errorMsg = String("Unexpected character '") + *p + "' in JSON";
            return false;
        }

        // Skip whitespace, expect ',' or '}'
        p = jsonSkip(p);
        if (*p == ',') {
            p++;
        } else if (*p == '}') {
            return true;
        } else {
            errorMsg = String("Expected ',' or '}' but got '") + *p + "'";
            return false;
        }
    }

    errorMsg = "Unexpected end of JSON";
    return false;
}

#endif // JSON_PARSER_H
