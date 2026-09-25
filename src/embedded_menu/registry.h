#pragma once

#include "cmd.h"
#include "handler.h"

#include <array>
#include <string.h>

namespace emenu {

enum class Result {
    OK = 0,
    NOT_FOUND,
    TABLE_FULL,
    DUPLICATE,
    INVALID_ARG,
};

/// Fixed-capacity command registry. No heap allocation.
template <size_t N = 32>
class Registry {
public:
    Registry() = default;

    Result add(const char* name, const RegistrationOptions& opts) {
        if (!name || !opts.handler) return Result::INVALID_ARG;
        if (find(name)) return Result::DUPLICATE;
        if (_count >= N) return Result::TABLE_FULL;

        auto& e = _entries[_count];
        e.name = name;
        e.handler = opts.handler;
        e.help = opts.help;
        e.group = opts.group;
        for (size_t i = 0; i < EMENU_MAX_ALIASES; i++) {
            e.aliases[i] = opts.aliases[i];
        }
        for (size_t i = 0; i < EMENU_MAX_PARAM_DEFS; i++) {
            e.params[i] = opts.params[i];
        }
        e.access = opts.access;
        _count++;
        return Result::OK;
    }

    Result execute(const char* name, Cmd& cmd) const {
        const auto* entry = find(name);
        if (!entry) return Result::NOT_FOUND;
        cmd.set_command_name(name);
        entry->handler(cmd);
        return Result::OK;
    }

    const CommandEntry* find(const char* name) const {
        if (!name) return nullptr;
        for (size_t i = 0; i < _count; i++) {
            if (_entries[i].matches(name)) return &_entries[i];
        }
        return nullptr;
    }

    size_t count() const { return _count; }
    constexpr size_t capacity() const { return N; }

    const CommandEntry* begin() const { return _entries.data(); }
    const CommandEntry* end() const { return _entries.data() + _count; }

    size_t find_by_group(const char* group, const CommandEntry** out,
                         size_t out_capacity) const {
        size_t n = 0;
        for (size_t i = 0; i < _count && n < out_capacity; i++) {
            if (_entries[i].group && strcmp(_entries[i].group, group) == 0) {
                out[n++] = &_entries[i];
            }
        }
        return n;
    }

private:
    std::array<CommandEntry, N> _entries{};
    size_t _count = 0;
};

}  // namespace emenu
