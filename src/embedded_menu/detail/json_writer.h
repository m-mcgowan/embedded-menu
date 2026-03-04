#pragma once

#include "../writer.h"
#include <stdio.h>
#include <string.h>

namespace emenu {
namespace detail {

/// Streaming JSON writer — writes directly to a Writer with zero buffering.
/// Supports nested objects and arrays with automatic comma insertion.
///
/// Usage:
///   JsonWriter jw(output);
///   jw.begin_object()
///     .field("cmd", "help")
///     .key("commands").begin_array()
///       .begin_object().field("name", "ping").end_object()
///       .begin_object().field("name", "echo").end_object()
///     .end_array()
///   .end_object();
class JsonWriter {
public:
    explicit JsonWriter(Writer& out) : _out(out), _depth(0) {}

    /// Start the root object. Shorthand for begin_object().
    JsonWriter& begin() { return begin_object(); }
    /// End the root object and emit newline + end_frame.
    JsonWriter& end() {
        end_object();
        _out.write(static_cast<uint8_t>('\n'));
        _out.end_frame();
        return *this;
    }

    JsonWriter& begin_object() {
        _comma();
        _out.write(static_cast<uint8_t>('{'));
        _push();
        return *this;
    }

    JsonWriter& end_object() {
        _pop();
        _out.write(static_cast<uint8_t>('}'));
        return *this;
    }

    JsonWriter& begin_array() {
        _comma();
        _out.write(static_cast<uint8_t>('['));
        _push();
        return *this;
    }

    JsonWriter& end_array() {
        _pop();
        _out.write(static_cast<uint8_t>(']'));
        return *this;
    }

    /// Emit a key (for use before begin_object/begin_array/value).
    JsonWriter& key(const char* k) {
        _comma();
        _out.write(static_cast<uint8_t>('"'));
        _escaped(k);
        _out.print("\":");
        _after_key = true;  // suppress comma for the following value
        return *this;
    }

    JsonWriter& field(const char* k, const char* value) {
        _comma();
        _out.write(static_cast<uint8_t>('"'));
        _escaped(k);
        _out.print("\":");
        if (value) {
            _out.write(static_cast<uint8_t>('"'));
            _escaped(value);
            _out.write(static_cast<uint8_t>('"'));
        } else {
            _out.print("null");
        }
        return *this;
    }

    JsonWriter& field(const char* k, int value) {
        _comma();
        _out.write(static_cast<uint8_t>('"'));
        _escaped(k);
        _out.print("\":");
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", value);
        _out.print(buf);
        return *this;
    }

    JsonWriter& field(const char* k, bool value) {
        _comma();
        _out.write(static_cast<uint8_t>('"'));
        _escaped(k);
        _out.print("\":");
        _out.print(value ? "true" : "false");
        return *this;
    }

    JsonWriter& field(const char* k, float value, int digits = 4) {
        _comma();
        _out.write(static_cast<uint8_t>('"'));
        _escaped(k);
        _out.print("\":");
        char buf[24];
        snprintf(buf, sizeof(buf), "%.*f", digits, static_cast<double>(value));
        _out.print(buf);
        return *this;
    }

    /// Emit a null value (for use after key()).
    JsonWriter& null_value() {
        _comma();
        _out.print("null");
        return *this;
    }

    /// Emit a string value (for use after key() or inside arrays).
    JsonWriter& value(const char* v) {
        _comma();
        if (v) {
            _out.write(static_cast<uint8_t>('"'));
            _escaped(v);
            _out.write(static_cast<uint8_t>('"'));
        } else {
            _out.print("null");
        }
        return *this;
    }

private:
    Writer& _out;

    static constexpr size_t MAX_DEPTH = 4;
    bool _first[MAX_DEPTH];
    size_t _depth;
    bool _after_key = false;  // true after key(), suppresses next comma

    void _push() {
        if (_depth < MAX_DEPTH) {
            _first[_depth] = true;
            _depth++;
        }
    }

    void _pop() {
        if (_depth > 0) _depth--;
    }

    void _comma() {
        if (_after_key) {
            _after_key = false;
            return;  // key already handled the comma
        }
        if (_depth > 0 && !_first[_depth - 1]) {
            _out.write(static_cast<uint8_t>(','));
        }
        if (_depth > 0) _first[_depth - 1] = false;
    }

    void _escaped(const char* s) {
        while (*s) {
            switch (*s) {
                case '"':  _out.print("\\\""); break;
                case '\\': _out.print("\\\\"); break;
                case '\n': _out.print("\\n"); break;
                case '\r': _out.print("\\r"); break;
                case '\t': _out.print("\\t"); break;
                default:
                    if (static_cast<unsigned char>(*s) < 0x20) {
                        char esc[7];
                        snprintf(esc, sizeof(esc), "\\u%04x",
                                 static_cast<unsigned char>(*s));
                        _out.print(esc);
                    } else {
                        _out.write(static_cast<uint8_t>(*s));
                    }
                    break;
            }
            s++;
        }
    }
};

}  // namespace detail
}  // namespace emenu
