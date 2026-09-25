#pragma once

#include "../cmd.h"
#include "../detail/json_parser.h"
#include "../detail/json_writer.h"
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

    /// Whether the TUI should be active. Firmware checks this to decide
    /// whether to render menus, prompts, and other unsolicited output.
    /// Defaults to true. A JSON client sends {"cmd":"tui","enabled":false}
    /// to suppress TUI output while it drives commands programmatically.
    bool tui_enabled() const { return _tui_enabled; }

    /// The session's access level. A command whose `access` is above it is
    /// refused ("not permitted") without running, and `help` does not list it.
    /// Defaults to the maximum, so a registry without access levels behaves as
    /// before.
    void set_access(uint8_t level) { _access = level; }
    uint8_t access() const { return _access; }

    void reset() { _line_pos = 0; }

private:
    bool _permitted(const CommandEntry& entry) const { return entry.access <= _access; }

    bool _tui_enabled = true;
    uint8_t _access = 0xFF;
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
        const char* topic = nullptr;
        for (int i = 0; i < n; i++) {
            if (strcmp(pairs[i].key, "cmd") == 0) {
                cmd_name = pairs[i].value;
            } else if (strcmp(pairs[i].key, "topic") == 0) {
                topic = pairs[i].value;
            }
        }
        if (!cmd_name || !*cmd_name) {
            _emit_error(nullptr, "missing cmd");
            return;
        }

        // Built-in: help
        if (strcmp(cmd_name, "help") == 0) {
            _help(topic);
            return;
        }

        // Built-in: tui
        if (strcmp(cmd_name, "tui") == 0) {
            _tui(pairs, n);
            return;
        }

        // Build Cmd with remaining fields as params
        Cmd cmd(_output);
        cmd.set_command_name(cmd_name);
        for (int i = 0; i < n; i++) {
            if (strcmp(pairs[i].key, "cmd") != 0 &&
                strcmp(pairs[i].key, "topic") != 0) {
                cmd.set_param(pairs[i].key, pairs[i].value);
            }
        }

        // Dispatch, if the session may run it
        const CommandEntry* entry = _registry.find(cmd_name);
        if (!entry) {
            _emit_error(cmd_name, "not found");
            return;
        }
        if (!_permitted(*entry)) {
            _emit_error(cmd_name, "not permitted");
            return;
        }
        _registry.execute(cmd_name, cmd);

        // Stream response directly to output
        detail::JsonWriter jw(_output);
        jw.begin().field("cmd", cmd_name);

        if (cmd.has_reply()) {
            for (size_t i = 0; i < cmd.reply_count(); i++) {
                const auto& f = cmd.reply_field(i);
                // Detect type from value string
                if (!f.value) {
                    jw.field(f.key, static_cast<const char*>(nullptr));
                } else if (strcmp(f.value, "true") == 0) {
                    jw.field(f.key, true);
                } else if (strcmp(f.value, "false") == 0) {
                    jw.field(f.key, false);
                } else {
                    // Try int
                    char* end = nullptr;
                    long lv = strtol(f.value, &end, 10);
                    if (end != f.value && *end == '\0') {
                        jw.field(f.key, static_cast<int>(lv));
                    } else {
                        // Try float
                        float fv = strtof(f.value, &end);
                        if (end != f.value && *end == '\0') {
                            jw.field(f.key, fv);
                        } else {
                            jw.field(f.key, f.value);
                        }
                    }
                }
            }
        } else {
            jw.field("ok", true);
        }

        jw.end();
    }

    void _tui(const detail::JsonPair* pairs, int n) {
        // Look for "enabled" param
        for (int i = 0; i < n; i++) {
            if (strcmp(pairs[i].key, "enabled") == 0) {
                _tui_enabled =
                    strcmp(pairs[i].value, "true") == 0 ||
                    strcmp(pairs[i].value, "1") == 0;
            }
        }
        // Always respond with current state
        detail::JsonWriter jw(_output);
        jw.begin().field("cmd", "tui").field("enabled", _tui_enabled).end();
    }

    void _help(const char* topic) {
        if (topic && *topic) {
            // Single command detail
            const auto* entry = _registry.find(topic);
            if (!entry || !_permitted(*entry)) {
                _emit_error("help", "not found");
                return;
            }
            detail::JsonWriter jw(_output);
            jw.begin().field("cmd", "help");
            _emit_entry_fields(jw, *entry);
            jw.end();
            return;
        }

        // List all commands
        detail::JsonWriter jw(_output);
        jw.begin().field("cmd", "help");
        jw.key("commands").begin_array();
        for (const auto& entry : _registry) {
            if (!_permitted(entry)) continue;
            jw.begin_object();
            _emit_entry_fields(jw, entry);
            jw.end_object();
        }
        jw.end_array();
        jw.end();
    }

    void _emit_entry_fields(detail::JsonWriter& jw, const CommandEntry& entry) {
        jw.field("name", entry.name);
        jw.field("help", entry.help);  // null-safe
        jw.field("group", entry.group);  // null-safe
        jw.key("aliases").begin_array();
        for (size_t i = 0; i < EMENU_MAX_ALIASES && entry.aliases[i]; i++) {
            jw.value(entry.aliases[i]);
        }
        jw.end_array();
        jw.key("params").begin_array();
        for (size_t i = 0; i < EMENU_MAX_PARAM_DEFS && entry.params[i].valid(); i++) {
            const auto& p = entry.params[i];
            jw.begin_object()
              .field("name", p.name)
              .field("type", p.type)
              .field("required", p.required);
            if (p.help)        jw.field("help", p.help);
            if (p.default_val) jw.field("default", p.default_val);
            jw.end_object();
        }
        jw.end_array();
    }

    void _emit_error(const char* cmd_name, const char* msg) {
        detail::JsonWriter jw(_output);
        jw.begin();
        if (cmd_name) jw.field("cmd", cmd_name);
        jw.field("error", msg);
        jw.end();
    }
};

}  // namespace emenu
