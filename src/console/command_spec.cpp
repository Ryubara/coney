// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/command_spec.h"

namespace coney::console {

// One word per value, as the catalog spells them (commands.toml).
std::string_view accessName(Access access) { return access == Access::Player ? "player" : "developer"; }

// The scope's word.
std::string_view scopeName(Scope scope) { return scope == Scope::Local ? "local" : "session"; }

// The type's word, as console_catalog.ARG_TYPES spells it.
std::string_view argTypeName(ArgType type) {
    switch (type) {
    case ArgType::Int:
        return "int";
    case ArgType::Number:
        return "number";
    case ArgType::Bool:
        return "bool";
    case ArgType::Text:
        return "text";
    case ArgType::Choice:
        return "choice";
    case ArgType::Mission:
        return "mission";
    case ArgType::Checkpoint:
        return "checkpoint";
    case ArgType::Level:
        return "level";
    case ArgType::Mod:
        return "mod";
    case ArgType::Setting:
        return "setting";
    case ArgType::Command:
        return "command";
    }
    return "text";
}

// The need's word.
std::string_view needName(Need need) {
    switch (need) {
    case Need::Game:
        return "game";
    case Need::Profile:
        return "profile";
    case Need::Level:
        return "level";
    case Need::Disc:
        return "disc";
    }
    return "game";
}

// Lower-case letters, digits, - and _, starting with a letter.
bool isCommandName(std::string_view name, std::size_t maxLength) {
    if (name.empty() || name.size() > maxLength || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    for (const char c : name) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

} // namespace coney::console
