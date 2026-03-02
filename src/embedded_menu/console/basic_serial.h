#pragma once

#include "../cmd.h"
#include "../registry.h"

#include <string.h>

namespace emenu {

/// Simple line-buffer console for interactive use.
///
/// Reads characters, buffers until newline, splits on whitespace into
/// argc/argv, and dispatches to the registry. No line editing, no history.
///
/// Built-in commands: "help" lists all registered commands.
template <size_t N = 32>
class BasicSerial {
public:
    BasicSerial(Registry<N>& registry, Writer& output)
        : _registry(registry), _output(output), _line_pos(0) {}

    void process_byte(uint8_t c) {
        if (c == '\n' || c == '\r') {
            if (_line_pos > 0) {
                _line_buf[_line_pos] = '\0';
                _dispatch(_line_buf);
                _line_pos = 0;
            }
            if (_show_prompt) prompt();
            return;
        }
        if (c == '\b' || c == 127) {  // backspace / delete
            if (_line_pos > 0) {
                _line_pos--;
                if (_echo) _output.print("\b \b");
            }
            return;
        }
        if (_line_pos < LINE_BUF_SIZE - 1) {
            _line_buf[_line_pos++] = static_cast<char>(c);
            if (_echo) _output.write(c);
        }
    }

    void process_line(const char* line) {
        // Copy to mutable buffer for tokenization
        size_t len = strlen(line);
        if (len >= LINE_BUF_SIZE) len = LINE_BUF_SIZE - 1;
        memcpy(_line_buf, line, len);
        _line_buf[len] = '\0';
        _dispatch(_line_buf);
    }

    void prompt() { _output.print(_prompt); }

    void set_prompt(const char* p) { _prompt = p; }
    void set_echo(bool e) { _echo = e; }
    void set_show_prompt(bool s) { _show_prompt = s; }

    Writer& output() { return _output; }
    void reset() { _line_pos = 0; }

private:
    Registry<N>& _registry;
    Writer& _output;
    const char* _prompt = "> ";
    bool _echo = false;
    bool _show_prompt = false;

    static constexpr size_t LINE_BUF_SIZE = 128;
    char _line_buf[LINE_BUF_SIZE];
    size_t _line_pos;

    static constexpr size_t MAX_ARGS = 16;

    void _dispatch(char* line) {
        // Strip leading whitespace
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) return;

        // Tokenize on whitespace
        char* args[MAX_ARGS];
        int argc = 0;
        char* p = line;
        while (*p && static_cast<size_t>(argc) < MAX_ARGS) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            args[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
        if (argc == 0) return;

        // Built-in: help
        if (strcmp(args[0], "help") == 0) {
            _help(argc > 1 ? args[1] : nullptr);
            _output.end_frame();
            return;
        }

        Cmd cmd(_output);
        cmd.set_argv(argc, args);
        Result result = _registry.execute(args[0], cmd);
        if (result == Result::NOT_FOUND) {
            _output.printf("Unknown command: %s\r\n", args[0]);
        }
        _output.end_frame();
    }

    void _help(const char* topic) {
        if (topic) {
            const auto* entry = _registry.find(topic);
            if (entry) {
                _output.printf("%s", entry->name);
                if (entry->help) _output.printf(" — %s", entry->help);
                _output.print("\r\n");
                // Show aliases
                for (size_t i = 0; i < EMENU_MAX_ALIASES && entry->aliases[i]; i++) {
                    _output.printf("  alias: %s\r\n", entry->aliases[i]);
                }
            } else {
                _output.printf("Unknown command: %s\r\n", topic);
            }
            return;
        }

        // List all commands
        const char* last_group = nullptr;
        for (const auto& entry : _registry) {
            if (entry.group && (!last_group || strcmp(entry.group, last_group) != 0)) {
                _output.printf("\r\n[%s]\r\n", entry.group);
                last_group = entry.group;
            }
            _output.printf("  %-20s", entry.name);
            if (entry.help) _output.printf(" %s", entry.help);
            _output.print("\r\n");
        }
    }
};

}  // namespace emenu
