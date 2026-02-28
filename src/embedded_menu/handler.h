#pragma once

#include <stddef.h>
#include <string.h>

namespace emenu {

class Cmd;

/// Handler type — function pointer by default, std::function opt-in.
#ifdef EMENU_USE_STD_FUNCTION
#include <functional>
using Handler = std::function<void(Cmd&)>;
#else
using Handler = void (*)(Cmd&);
#endif

#ifndef EMENU_MAX_ALIASES
#define EMENU_MAX_ALIASES 4
#endif

/// A registered command with all its metadata.
/// All string pointers must outlive the registry (typically string literals).
struct CommandEntry {
    const char* name = nullptr;
    Handler handler = nullptr;
    const char* help = nullptr;
    const char* group = nullptr;
    const char* aliases[EMENU_MAX_ALIASES] = {};

    bool valid() const { return name != nullptr && handler != nullptr; }

    bool matches(const char* query) const {
        if (!query || !name) return false;
        if (strcmp(name, query) == 0) return true;
        for (size_t i = 0; i < EMENU_MAX_ALIASES && aliases[i]; i++) {
            if (strcmp(aliases[i], query) == 0) return true;
        }
        return false;
    }
};

/// Options passed to Registry::add(). Separate from CommandEntry so the
/// caller doesn't need to provide the name twice.
struct RegistrationOptions {
    Handler handler = nullptr;
    const char* help = nullptr;
    const char* group = nullptr;
    const char* aliases[EMENU_MAX_ALIASES] = {};
};

}  // namespace emenu
