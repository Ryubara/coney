// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/message_handlers.h"

#include <optional>
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

void MessageHandlers::setPrompt(double object, std::string_view callback, std::string_view prompt,
                                std::string_view hint) {
    if (callback.empty() || prompt.empty()) {
        m_prompts.erase(object);
        m_promptHints.erase(object);
        return;
    }
    // One record per object: a second registration keeps the first's texts.
    if (m_prompts.try_emplace(object, prompt).second && !hint.empty()) {
        m_promptHints.try_emplace(object, hint);
    }
}

std::string_view MessageHandlers::handler(double object, int message) const {
    const auto found = m_handlers.find({object, message});
    return found == m_handlers.end() ? std::string_view{} : std::string_view(found->second);
}

void MessageHandlers::setGeneralCar(int message, std::string callback) {
    if (message < 0 || message >= kMessages) {
        return;
    }
    if (callback.empty()) {
        m_generalCar.erase(message);
        return;
    }
    m_generalCar[message] = std::move(callback);
}

std::string_view MessageHandlers::generalCarHandler(int message) const {
    const auto found = m_generalCar.find(message);
    return found == m_generalCar.end() ? std::string_view{} : std::string_view(found->second);
}

bool MessageHandlers::deliverFromCar(ScriptSystem& scripts, double car, int message, double other, double value,
                                     bool flag) const {
    const double number = flag ? 1.0 : 0.0;
    const std::string_view own = handler(car, message);
    const bool ownTook = !own.empty() && call(scripts, own, car, message, car, other, value, number);
    const std::string_view general = generalCarHandler(message);
    const bool any = !general.empty() && call(scripts, general, car, message, car, other, value, number);
    return ownTook || any;
}

bool MessageHandlers::deliver(ScriptSystem& scripts, double object, int message, double subject, double other,
                              double value) const {
    const std::string_view function = handler(object, message);
    if (function.empty()) {
        return false;
    }
    return call(scripts, function, object, message, subject, other, value);
}

bool MessageHandlers::call(ScriptSystem& scripts, std::string_view function, double object, int message, double subject,
                           double other, double value, double flag) {
    // The arguments by number (docs/research/scripting.md#message-handlers).
    std::vector<Value> args;
    bool asksResult = false;
    switch (message) {
    case 0:
        args = {Value(object), Value(subject)};
        asksResult = true;
        break;
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
        args = {Value(object), Value(other), Value(value), Value(flag)};
        break;
    default:
        args = {Value(object), Value(other)};
        break;
    }
    if (message == 0) {
        // The interaction asks one result: anything but nil takes the press.
        const std::optional<std::vector<Value>> results = scripts.callResults(function, args);
        return results.has_value() && !results->empty() && !results->front().isNil();
    }
    const bool ran = scripts.call(function, args);
    return asksResult && ran;
}

} // namespace coney::script
