#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EMENU_MAX_VALUE_LEN
/// Longest parameter value (unescaped, including the terminator) a JSON line can
/// carry. Values that do not fit are rejected (JSON_PARSE_TOO_LONG), never cut.
#define EMENU_MAX_VALUE_LEN 64
#endif

namespace emenu {
namespace detail {

/// json_parse_flat(): a key or value did not fit its buffer.
constexpr int JSON_PARSE_TOO_LONG = -2;

/// Parsed value type from JSON.
enum class JsonTokenType {
    STRING,
    INTEGER,
    FLOAT,
    BOOL_TRUE,
    BOOL_FALSE,
    NULL_VAL,
    ERROR,
};

/// A parsed key-value pair from a flat JSON object.
struct JsonPair {
    char key[32];
    char value[EMENU_MAX_VALUE_LEN];
    JsonTokenType type;
};

/// Skip whitespace, return pointer to next non-whitespace char.
inline const char* json_skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/// Parse a JSON string (starting after the opening "). Writes into buf.
/// Returns pointer past the closing " or nullptr on error. A string that does not
/// fit buf is an error too (too_long set), never a silently shortened value.
inline const char* json_parse_string(const char* p, char* buf, size_t buf_size,
                                     bool* too_long = nullptr) {
    size_t i = 0;
    bool overflow = false;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            char esc = 0;
            switch (*p) {
                case '"':  esc = '"'; break;
                case '\\': esc = '\\'; break;
                case '/':  esc = '/'; break;
                case 'n':  esc = '\n'; break;
                case 'r':  esc = '\r'; break;
                case 't':  esc = '\t'; break;
                default:   esc = *p; break;  // best-effort
            }
            if (i < buf_size - 1) buf[i++] = esc; else overflow = true;
            p++;
        } else {
            if (i < buf_size - 1) buf[i++] = *p; else overflow = true;
            p++;
        }
    }
    buf[i] = '\0';
    if (too_long) *too_long = overflow;
    if (*p != '"' || overflow) return nullptr;
    return p + 1;  // skip closing "
}

/// Parse a flat JSON object into key-value pairs.
/// Returns number of pairs parsed, -1 on a syntax error, or JSON_PARSE_TOO_LONG
/// when a key or value does not fit its buffer (EMENU_MAX_VALUE_LEN for values).
/// Only handles flat objects: {"key":"val","num":42,"flag":true}
/// @param parsed if given, set to the number of pairs completed before an error --
///        so a caller can still name the command of a line it rejects.
inline int json_parse_flat(const char* input, JsonPair* pairs, size_t max_pairs,
                           int* parsed = nullptr) {
    int count = 0;
    if (parsed) *parsed = 0;

    const char* p = json_skip_ws(input);
    if (*p != '{') return -1;
    p++;

    p = json_skip_ws(p);
    if (*p == '}') return 0;  // empty object

    while (*p) {
        if (static_cast<size_t>(count) >= max_pairs) return count;

        // Parse key
        p = json_skip_ws(p);
        if (*p != '"') return -1;
        p++;
        bool too_long = false;
        p = json_parse_string(p, pairs[count].key, sizeof(pairs[count].key), &too_long);
        if (!p) return too_long ? JSON_PARSE_TOO_LONG : -1;

        // Colon
        p = json_skip_ws(p);
        if (*p != ':') return -1;
        p++;

        // Parse value
        p = json_skip_ws(p);

        if (*p == '"') {
            // String value
            p++;
            p = json_parse_string(p, pairs[count].value, sizeof(pairs[count].value), &too_long);
            if (!p) return too_long ? JSON_PARSE_TOO_LONG : -1;
            pairs[count].type = JsonTokenType::STRING;
        } else if (*p == 't' && strncmp(p, "true", 4) == 0) {
            strcpy(pairs[count].value, "true");
            pairs[count].type = JsonTokenType::BOOL_TRUE;
            p += 4;
        } else if (*p == 'f' && strncmp(p, "false", 5) == 0) {
            strcpy(pairs[count].value, "false");
            pairs[count].type = JsonTokenType::BOOL_FALSE;
            p += 5;
        } else if (*p == 'n' && strncmp(p, "null", 4) == 0) {
            pairs[count].value[0] = '\0';
            pairs[count].type = JsonTokenType::NULL_VAL;
            p += 4;
        } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
            // Number — integer or float
            char* end = nullptr;
            const char* start = p;
            // Scan the number to detect float vs int
            bool is_float = false;
            const char* scan = p;
            if (*scan == '-') scan++;
            while (*scan >= '0' && *scan <= '9') scan++;
            if (*scan == '.' || *scan == 'e' || *scan == 'E') is_float = true;

            if (is_float) {
                strtod(start, &end);
                pairs[count].type = JsonTokenType::FLOAT;
            } else {
                strtol(start, &end, 10);
                pairs[count].type = JsonTokenType::INTEGER;
            }

            if (end == start) return -1;
            size_t vlen = static_cast<size_t>(end - start);
            if (vlen >= sizeof(pairs[count].value)) return JSON_PARSE_TOO_LONG;
            memcpy(pairs[count].value, start, vlen);
            pairs[count].value[vlen] = '\0';
            p = end;
        } else {
            return -1;  // unexpected character
        }

        count++;
        if (parsed) *parsed = count;

        // Comma or end
        p = json_skip_ws(p);
        if (*p == ',') {
            p++;
        } else if (*p == '}') {
            return count;
        } else {
            return -1;
        }
    }

    return -1;  // unterminated
}

/// Incremental JSON object builder into a fixed buffer.
class JsonBuilder {
public:
    JsonBuilder(char* buf, size_t capacity)
        : _buf(buf), _cap(capacity), _pos(0), _first(true), _overflow(false) {}

    JsonBuilder& begin() {
        _append_char('{');
        _first = true;
        return *this;
    }

    JsonBuilder& field(const char* key, const char* value) {
        _comma();
        _append_char('"');
        _append(key);
        _append("\":");
        if (value) {
            _append_char('"');
            _append_escaped(value);
            _append_char('"');
        } else {
            _append("null");
        }
        return *this;
    }

    JsonBuilder& field(const char* key, int value) {
        _comma();
        _append_char('"');
        _append(key);
        _append("\":");
        char num[16];
        snprintf(num, sizeof(num), "%d", value);
        _append(num);
        return *this;
    }

    JsonBuilder& field(const char* key, float value, int digits = 4) {
        _comma();
        _append_char('"');
        _append(key);
        _append("\":");
        char num[24];
        snprintf(num, sizeof(num), "%.*f", digits, static_cast<double>(value));
        _append(num);
        return *this;
    }

    JsonBuilder& field(const char* key, bool value) {
        _comma();
        _append_char('"');
        _append(key);
        _append("\":");
        _append(value ? "true" : "false");
        return *this;
    }

    JsonBuilder& end() {
        _append("}\n");
        return *this;
    }

    const char* str() const { return _buf; }
    size_t len() const { return _pos; }
    bool overflowed() const { return _overflow; }

private:
    char* _buf;
    size_t _cap;
    size_t _pos;
    bool _first;
    bool _overflow;

    void _comma() {
        if (!_first) _append_char(',');
        _first = false;
    }

    void _append_char(char c) {
        if (_pos < _cap - 1) {
            _buf[_pos++] = c;
            _buf[_pos] = '\0';
        } else {
            _overflow = true;
        }
    }

    void _append(const char* s) {
        while (*s) _append_char(*s++);
    }

    void _append_escaped(const char* s) {
        while (*s) {
            switch (*s) {
                case '"':  _append("\\\""); break;
                case '\\': _append("\\\\"); break;
                case '\n': _append("\\n"); break;
                case '\r': _append("\\r"); break;
                case '\t': _append("\\t"); break;
                default:
                    if (static_cast<unsigned char>(*s) < 0x20) {
                        // Escape control chars as \u00XX
                        char esc[7];
                        snprintf(esc, sizeof(esc), "\\u%04x",
                                 static_cast<unsigned char>(*s));
                        _append(esc);
                    } else {
                        _append_char(*s);
                    }
                    break;
            }
            s++;
        }
    }
};

}  // namespace detail
}  // namespace emenu
