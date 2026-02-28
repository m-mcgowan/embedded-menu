#pragma once

#include "writer.h"

#include <stdlib.h>
#include <string.h>

namespace emenu {

#ifndef EMENU_MAX_PARAMS
#define EMENU_MAX_PARAMS 16
#endif

/// A single named parameter (key + string value).
struct Param {
    const char* key = nullptr;
    const char* value = nullptr;
};

/// How the Cmd was constructed.
enum class CmdSource {
    ARGV,
    JSON,
    DIRECT,
};

/// Cmd is constructed by the transport layer and passed to the handler.
/// The handler reads params with param_*() and produces output with reply()/out().
class Cmd {
public:
    explicit Cmd(Writer& writer) : _writer(writer) {}

    // --- Parameter access (transport-agnostic) ---

    int param_int(const char* name, int default_val = 0) const {
        const char* v = _find_param(name);
        if (!v || !*v) return default_val;
        char* end = nullptr;
        long r = strtol(v, &end, 10);
        return (end != v) ? static_cast<int>(r) : default_val;
    }

    float param_float(const char* name, float default_val = 0.0f) const {
        const char* v = _find_param(name);
        if (!v || !*v) return default_val;
        char* end = nullptr;
        float r = strtof(v, &end);
        return (end != v) ? r : default_val;
    }

    const char* param_str(const char* name, const char* default_val = "") const {
        const char* v = _find_param(name);
        return v ? v : default_val;
    }

    bool param_bool(const char* name, bool default_val = false) const {
        const char* v = _find_param(name);
        if (!v || !*v) return default_val;
        if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0 ||
            strcmp(v, "yes") == 0 || strcmp(v, "on") == 0)
            return true;
        if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0 ||
            strcmp(v, "no") == 0 || strcmp(v, "off") == 0)
            return false;
        return default_val;
    }

    // --- Raw argv access (when source == ARGV) ---

    int argc() const { return _argc; }
    char** argv() const { return _argv; }

    // --- Output ---

    Writer& out() { return _writer; }

    void reply(const char* key, const char* value) {
        if (_reply_count >= EMENU_MAX_PARAMS) return;
        _reply_fields[_reply_count].key = key;
        _reply_fields[_reply_count].value = _store_reply(value);
        _reply_count++;
    }

    void reply(const char* key, int value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", value);
        reply(key, static_cast<const char*>(buf));
    }

    void reply(const char* key, float value) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
        reply(key, static_cast<const char*>(buf));
    }

    void reply(const char* key, bool value) {
        reply(key, value ? "true" : "false");
    }

    bool has_reply() const { return _reply_count > 0; }
    size_t reply_count() const { return _reply_count; }
    const Param& reply_field(size_t i) const { return _reply_fields[i]; }

    // --- Transport-facing API ---

    void set_argv(int argc, char** argv) {
        _source = CmdSource::ARGV;
        _argc = argc;
        _argv = argv;
        // Also populate named params from argv pairs: key value key value ...
        // argv[0] is the command name, so params start at argv[1]
        for (int i = 1; i + 1 < argc; i += 2) {
            set_param(argv[i], argv[i + 1]);
        }
    }

    bool set_param(const char* key, const char* value) {
        if (_param_count >= EMENU_MAX_PARAMS) return false;
        _params[_param_count].key = key;
        _params[_param_count].value = value;
        _param_count++;
        return true;
    }

    void set_command_name(const char* name) { _cmd_name = name; }
    const char* command_name() const { return _cmd_name; }

    CmdSource source() const { return _source; }

    void reset() {
        _source = CmdSource::DIRECT;
        _cmd_name = nullptr;
        _param_count = 0;
        _argc = 0;
        _argv = nullptr;
        _reply_count = 0;
        _reply_buf_pos = 0;
    }

private:
    Writer& _writer;
    CmdSource _source = CmdSource::DIRECT;
    const char* _cmd_name = nullptr;

    Param _params[EMENU_MAX_PARAMS];
    size_t _param_count = 0;

    int _argc = 0;
    char** _argv = nullptr;

    static constexpr size_t REPLY_BUF_SIZE = 512;
    Param _reply_fields[EMENU_MAX_PARAMS];
    size_t _reply_count = 0;
    char _reply_buf[REPLY_BUF_SIZE];
    size_t _reply_buf_pos = 0;

    const char* _find_param(const char* name) const {
        for (size_t i = 0; i < _param_count; i++) {
            if (_params[i].key && strcmp(_params[i].key, name) == 0)
                return _params[i].value;
        }
        return nullptr;
    }

    const char* _store_reply(const char* s) {
        if (!s) return nullptr;
        size_t len = strlen(s);
        if (_reply_buf_pos + len + 1 > REPLY_BUF_SIZE) return s;  // fallback: don't copy
        char* dest = _reply_buf + _reply_buf_pos;
        memcpy(dest, s, len + 1);
        _reply_buf_pos += len + 1;
        return dest;
    }
};

}  // namespace emenu
