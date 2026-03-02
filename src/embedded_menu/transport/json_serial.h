#pragma once

#include "../cmd.h"
#include "../detail/json_parser.h"
#include "../registry.h"

namespace emenu {

/// JSON lines transport over a byte stream.
///
/// Input:  {"cmd":"ping","value":42}\n
/// Output: {"cmd":"ping","ok":true}\n
///
/// Usage (Arduino):
///   JsonSerial<> js(registry, serial_writer);
///   void loop() { while (Serial.available()) js.process_byte(Serial.read()); }
///
/// Usage (native/test):
///   JsonSerial<> js(registry, buf_writer);
///   js.process_line(R"({"cmd":"ping"})");
template <size_t N = 32>
class JsonSerial {
public:
    JsonSerial(Registry<N>& registry, Writer& output)
        : _registry(registry), _output(output), _line_pos(0) {}

    void process_byte(uint8_t c) {
        if (c == '\n' || c == '\r') {
            if (_line_pos > 0) {
                _line_buf[_line_pos] = '\0';
                _dispatch(_line_buf);
                _line_pos = 0;
            }
            return;
        }
        if (_line_pos < LINE_BUF_SIZE - 1) {
            _line_buf[_line_pos++] = static_cast<char>(c);
        }
        // else: silently drop overflow characters until newline
    }

    void process_line(const char* line) {
        _dispatch(line);
    }

    Writer& output() { return _output; }

    void reset() { _line_pos = 0; }

private:
    Registry<N>& _registry;
    Writer& _output;

    static constexpr size_t LINE_BUF_SIZE = 256;
    char _line_buf[LINE_BUF_SIZE];
    size_t _line_pos;

    void _dispatch(const char* line) {
        detail::JsonPair pairs[EMENU_MAX_PARAMS];
        int n = detail::json_parse_flat(line, pairs, EMENU_MAX_PARAMS);
        if (n < 0) {
            _emit_error(nullptr, "parse error");
            return;
        }

        // Find the "cmd" field
        const char* cmd_name = nullptr;
        for (int i = 0; i < n; i++) {
            if (strcmp(pairs[i].key, "cmd") == 0) {
                cmd_name = pairs[i].value;
                break;
            }
        }
        if (!cmd_name || !*cmd_name) {
            _emit_error(nullptr, "missing cmd");
            return;
        }

        // Build Cmd with remaining fields as params
        Cmd cmd(_output);
        cmd.set_command_name(cmd_name);
        for (int i = 0; i < n; i++) {
            if (strcmp(pairs[i].key, "cmd") != 0) {
                cmd.set_param(pairs[i].key, pairs[i].value);
            }
        }

        // Dispatch
        Result result = _registry.execute(cmd_name, cmd);
        if (result == Result::NOT_FOUND) {
            _emit_error(cmd_name, "not found");
            return;
        }

        // Build response from reply fields
        char resp[256];
        detail::JsonBuilder jb(resp, sizeof(resp));
        jb.begin().field("cmd", cmd_name);

        if (cmd.has_reply()) {
            for (size_t i = 0; i < cmd.reply_count(); i++) {
                const auto& f = cmd.reply_field(i);
                // Detect type from value string
                if (!f.value) {
                    jb.field(f.key, static_cast<const char*>(nullptr));
                } else if (strcmp(f.value, "true") == 0) {
                    jb.field(f.key, true);
                } else if (strcmp(f.value, "false") == 0) {
                    jb.field(f.key, false);
                } else {
                    // Try int
                    char* end = nullptr;
                    long lv = strtol(f.value, &end, 10);
                    if (end != f.value && *end == '\0') {
                        jb.field(f.key, static_cast<int>(lv));
                    } else {
                        // Try float
                        float fv = strtof(f.value, &end);
                        if (end != f.value && *end == '\0') {
                            jb.field(f.key, fv);
                        } else {
                            jb.field(f.key, f.value);
                        }
                    }
                }
            }
        } else {
            jb.field("ok", true);
        }

        jb.end();
        _output.print(resp);
        _output.end_frame();
    }

    void _emit_error(const char* cmd_name, const char* msg) {
        char resp[128];
        detail::JsonBuilder jb(resp, sizeof(resp));
        jb.begin();
        if (cmd_name) jb.field("cmd", cmd_name);
        jb.field("error", msg);
        jb.end();
        _output.print(resp);
        _output.end_frame();
    }
};

}  // namespace emenu
