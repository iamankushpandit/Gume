#pragma once

/* Small parsing helpers shared by the serial console's three files
 * (AppRuntimeConsole*.cpp). Header-only and allocation-free: everything works
 * in place on the console's own line buffer. */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace ConsoleText {

// millis() wraps every ~49 days; compare by signed difference.
inline bool before(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) < 0;
}

/* Split in place on spaces; "double quotes" group a value with spaces in it.
 * No escapes -- a value cannot contain a quote, a fair price for a parser
 * this small. Returns the token count, or -1 on an unterminated quote or too
 * many tokens. */
inline int tokenize(char* s, char* argv[], size_t maxArgs) {
    size_t n = 0;
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        if (n == maxArgs) return -1;
        if (*s == '"') {
            ++s;
            argv[n++] = s;
            char* end = strchr(s, '"');
            if (!end) return -1;
            *end = '\0';
            s = end + 1;
        } else {
            argv[n++] = s;
            while (*s && *s != ' ') ++s;
            if (*s) *s++ = '\0';
        }
    }
    return static_cast<int>(n);
}

inline bool parseOnOff(const char* s, bool& out) {
    if (strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0) { out = true; return true; }
    if (strcasecmp(s, "off") == 0 || strcmp(s, "0") == 0) { out = false; return true; }
    return false;
}

// A whole decimal number in [lo, hi], nothing trailing.
inline bool parseRange(const char* s, long lo, long hi, long& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    const long v = strtol(s, &end, 10);
    if (!end || *end || v < lo || v > hi) return false;
    out = v;
    return true;
}

/* A PIN's digit count is not derivable from its value -- 0000 and an empty
 * field are both zero -- so only exactly four digits are judged at all. */
inline bool fourDigits(const char* s, uint16_t& out) {
    if (strlen(s) != 4) return false;
    uint16_t v = 0;
    for (int i = 0; i < 4; ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
        v = static_cast<uint16_t>(v * 10 + (s[i] - '0'));
    }
    out = v;
    return true;
}

// Printable ASCII, no double quote (the reply grammar has no escapes).
inline bool printableName(const char* s, size_t maxLen) {
    const size_t len = strlen(s);
    if (len == 0 || len > maxLen) return false;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] < 0x20 || s[i] > 0x7E || s[i] == '"') return false;
    }
    return true;
}

}  // namespace ConsoleText
