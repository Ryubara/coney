// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/message_handlers.h"

#include <vector>

#include "scripting/lua_value.h"
#include "scripting/script_system.h"

namespace coney::script {

void MessageHandlers::set(double object, int message, std::string callback) {
    if (message < 0 || message >= kMessages) {
        return;
    }
    if (callback.empty()) {
        m_handlers.erase({object, message});
        return;
    }
    m_handlers[{object, message}] = std::move(callback);
}

std::string_view MessageHandlers::handler(double object, int message) const {
    const auto found = m_handlers.find({object, message});
    return found == m_handlers.end() ? std::string_view{} : std::string_view(found->second);
}

bool MessageHandlers::deliver(ScriptSystem& scripts, double object, int message, double subject, double other,
                              double value) const {
    const std::string_view function = handler(object, message);
    if (function.empty()) {
        return false;
    }
    // The arguments by number (docs/research/scripting.md#message-handlers).
    std::vector<Value> args;
    bool asksResult = false;
    switch (message) {
    case 0:
    case 3:
    case 4:
    case 5:
    case 8:
        args = {Value(object), Value(subject)};
        break;
    case 1:
        args = {Value(object), Value(other), Value(value)};
        break;
    case 6:
        args = {Value(subject), Value(value), Value(other)};
        break;
    case 7:
    case 0x12:
    case 0x13:
        args = {Value(subject), Value(other)};
        break;
    case 9:
        args = {Value(value)};
        asksResult = true;
        break;
    case 0xa:
    case 0xd:
        args = {Value(object)};
        break;
    case 0xc:
        args = {Value(object)};
        asksResult = true;
        break;
    case 0xe:
        args = {Value(object), Value(other), Value(value)};
        asksResult = true;
        break;
    case 0xf:
        args = {Value(subject), Value(other)};
        asksResult = true;
        break;
    case 0x10:
        args = {Value(subject), Value(other), Value(value)};
        asksResult = true;
        break;
    case 0x11:
        args = {Value(subject), Value(other), Value(value)};
        break;
    case 0x19:
        args = {Value(object), Value(other), Value(value), Value()};
        break;
    default:
        args = {Value(object), Value(other)};
        break;
    }
    const bool ran = scripts.call(function, args);
    return asksResult && ran;
}

} // namespace coney::script
