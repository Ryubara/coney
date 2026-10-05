// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/native_caller.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <utility>

#include "core/assert.h"
#include "debug/menu_model.h"

namespace coney::debug {

namespace {

// A default's text as a number; 0 when it is not one (`true` counts as 1).
double defaultNumber(std::string_view text) {
    if (text == "true") {
        return 1.0;
    }
    return parseNumber(text).value_or(0.0);
}

// A number in its shortest form, as Lua 4.0 prints it (`%.14g`).
std::string formatNumber(double number) { return std::format("{:.14g}", number); }

} // namespace

const NativeSignature* findNativeSignature(std::string_view name) {
    const auto all = nativeSignatures();
    const auto found = std::ranges::find(all, name, &NativeSignature::name);
    return found == all.end() ? nullptr : &*found;
}

std::string_view nativeArgTypeName(NativeArgType type) {
    static constexpr std::array<std::string_view, 8> kNames{"number", "integer",      "handle",       "boolean",
                                                            "string", "number table", "string table", "userdata"};
    return kNames.at(static_cast<std::size_t>(type));
}

NativeStatus nativeStatus(std::string_view name) {
    const auto table = script::bindingTable();
    const auto found = std::ranges::find(table, name, &script::BindingInfo::name);
    if (found == table.end()) {
        return NativeStatus::Missing;
    }
    switch (found->kind) {
    case script::BindingKind::Real:
        return NativeStatus::Implemented;
    case script::BindingKind::Routed:
        return NativeStatus::Partial;
    case script::BindingKind::Stub:
        return NativeStatus::Stub;
    }
    return NativeStatus::Missing;
}

std::string_view nativeStatusName(NativeStatus status) {
    static constexpr std::array<std::string_view, 4> kNames{"implemented", "partial", "stub", "missing"};
    return kNames.at(static_cast<std::size_t>(status));
}

std::string formatValue(const script::Value& value) {
    switch (value.type()) {
    case script::Value::Type::Nil:
        return "nil";
    case script::Value::Type::Number:
        return formatNumber(value.number().value_or(0.0));
    case script::Value::Type::String:
        return std::format("\"{}\"", value.string().value_or(std::string_view{}));
    case script::Value::Type::Function:
        return "function";
    case script::Value::Type::Table: {
        // The first entries in order; nested tables are not opened, so a cycle cannot loop.
        constexpr std::size_t kShown = 8;
        const script::Table& table = *value.table();
        std::string text = "{";
        std::size_t shown = 0;
        for (auto entry = table.next(script::Value()); entry; entry = table.next(entry->first)) {
            if (shown == kShown) {
                text += ", ...";
                break;
            }
            const bool nested = entry->second.type() == script::Value::Type::Table;
            text += std::format("{}{}={}", shown == 0 ? "" : ", ", formatValue(entry->first),
                                nested ? std::string("{...}") : formatValue(entry->second));
            ++shown;
        }
        return text + "}";
    }
    }
    return "?";
}

NativeArguments::NativeArguments(const NativeSignature& signature) : m_signature(&signature) {
    m_values.reserve(signature.args.size());
    for (const NativeArg& arg : signature.args) {
        Argument value;
        switch (arg.type) {
        case NativeArgType::Number:
        case NativeArgType::Integer:
        case NativeArgType::Handle:
            value.number = defaultNumber(arg.defaultValue);
            break;
        case NativeArgType::Boolean:
            value.flag = arg.defaultValue == "true" || defaultNumber(arg.defaultValue) != 0.0;
            break;
        case NativeArgType::String:
        case NativeArgType::StringTable:
            value.text = std::string(arg.defaultValue);
            break;
        case NativeArgType::NumberTable:
            value.numbers.assign(arg.count, 0.0);
            break;
        case NativeArgType::Userdata:
            break;
        }
        m_values.push_back(std::move(value));
    }
}

std::vector<script::Value> NativeArguments::toValues() {
    std::vector<script::Value> values;
    m_tables.assign(m_values.size(), nullptr);
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        const Argument& value = m_values[i];
        if (value.omitted) {
            break;
        }
        switch (m_signature->args[i].type) {
        case NativeArgType::Number:
        case NativeArgType::Integer:
        case NativeArgType::Handle:
            values.emplace_back(value.number);
            break;
        case NativeArgType::Boolean:
            values.push_back(value.flag ? script::Value(1.0) : script::Value());
            break;
        case NativeArgType::String:
            values.emplace_back(value.text);
            break;
        case NativeArgType::NumberTable:
        case NativeArgType::StringTable: {
            // Lua arrays count from 1.
            auto table = std::make_shared<script::Table>();
            if (m_signature->args[i].type == NativeArgType::NumberTable) {
                for (std::size_t n = 0; n < value.numbers.size(); ++n) {
                    // A whole-number key is never nil or NaN, so the store cannot fail.
                    const bool stored =
                        table->set(script::Value(static_cast<double>(n + 1)), script::Value(value.numbers[n]))
                            .has_value();
                    CONEY_ASSERT(stored);
                }
            } else {
                std::string_view rest = value.text;
                for (double n = 1; !rest.empty(); ++n) {
                    const auto comma = rest.find(',');
                    const bool stored =
                        table->set(script::Value(n), script::Value(std::string(rest.substr(0, comma)))).has_value();
                    CONEY_ASSERT(stored);
                    rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
                }
            }
            m_tables[i] = table;
            values.emplace_back(std::move(table));
            break;
        }
        case NativeArgType::Userdata:
            values.emplace_back();
            break;
        }
    }
    return values;
}

std::string NativeArguments::callText() const {
    std::string text = std::string(m_signature->name) + "(";
    for (std::size_t i = 0; i < m_values.size() && !m_values[i].omitted; ++i) {
        const Argument& value = m_values[i];
        std::string arg;
        switch (m_signature->args[i].type) {
        case NativeArgType::Number:
        case NativeArgType::Integer:
        case NativeArgType::Handle:
            arg = formatNumber(value.number);
            break;
        case NativeArgType::Boolean:
            arg = value.flag ? "1" : "nil";
            break;
        case NativeArgType::String:
            arg = std::format("\"{}\"", value.text);
            break;
        case NativeArgType::NumberTable: {
            arg = "{";
            for (std::size_t n = 0; n < value.numbers.size(); ++n) {
                arg += (n == 0 ? "" : ", ") + formatNumber(value.numbers[n]);
            }
            arg += "}";
            break;
        }
        case NativeArgType::StringTable:
            arg = "{" + value.text + "}";
            break;
        case NativeArgType::Userdata:
            arg = "nil";
            break;
        }
        text += (i == 0 ? "" : ", ") + arg;
    }
    return text + ")";
}

std::expected<std::vector<script::Value>, Error> callNative(script::LuaVm& vm, std::string_view name,
                                                            std::span<const script::Value> args) {
    const script::Value function = vm.global(name);
    if (function.isNil()) {
        return fail(ErrorCode::NotFound, std::format("{} is not registered in Coney (missing)", name));
    }
    return vm.call(function, args);
}

void DebugLog::add(std::string line) {
    m_lines.push_back(std::move(line));
    while (m_lines.size() > kCapacity) {
        m_lines.pop_front();
    }
}

std::vector<std::string> DebugLog::last(std::size_t count) const {
    const std::size_t from = m_lines.size() > count ? m_lines.size() - count : 0;
    return {m_lines.begin() + static_cast<std::ptrdiff_t>(from), m_lines.end()};
}

SandboxScripts::SandboxScripts(std::function<void(std::string_view)> log)
    : m_log(std::move(log)), m_context{&m_state, &m_strings, this, &m_recorded},
      m_scripts(
          [](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
              return fail(ErrorCode::NotFound, std::format("{}: the debug sandbox has no scripts", name));
          },
          [this](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, m_context); },
          [this](std::string_view line) {
              if (m_log) {
                  m_log(line);
              }
          }) {
    if (!m_log) {
        m_log = [](std::string_view) {};
    }
    m_scripts.create();
}

void SandboxScripts::showProfileManager(std::string_view onRumble, std::string_view onStartGame) {
    m_log(std::format("sandbox: ShowProfileManager({}, {})", onRumble, onStartGame));
}

void SandboxScripts::showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double players) {
    m_log(std::format("sandbox: ShowRumbleModeInterface({}, {}, {})", onCancel, onStart, players));
}

void SandboxScripts::menuLoadLevel(std::string_view level) { m_log(std::format("sandbox: MenuLoadLevel({})", level)); }

void SandboxScripts::playMovie(std::string_view name) { m_log(std::format("sandbox: PlayMovie({})", name)); }

void SandboxScripts::playMusic(std::string_view track) { m_log(std::format("sandbox: music {}", track)); }

void SandboxScripts::stopMusic() { m_log("sandbox: music stopped"); }

void SandboxScripts::queueScreenEffect(int type, double seconds) {
    m_log(std::format("sandbox: ScreenQueueEffect({}, {})", type, seconds));
}

} // namespace coney::debug
