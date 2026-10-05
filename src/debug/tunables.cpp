// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/tunables.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>

#include "core/assert.h"
#include "core/parse_number.h"

namespace coney::debug {

namespace {

// `text` without spaces and tabs at either end.
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// An override's value: a number in C notation, or on/off/true/false for a bool.
std::optional<double> parseValue(std::string_view text) {
    if (text == "on" || text == "true") {
        return 1.0;
    }
    if (text == "off" || text == "false") {
        return 0.0;
    }
    return parseDecimal(text);
}

} // namespace

Tunable::Tunable(std::string category, std::string name, Target target)
    : m_category(std::move(category)), m_name(std::move(name)), m_target(target) {
    m_default = current();
    if (type() == TunableType::Bool) {
        m_min = 0.0;
        m_max = 1.0;
    } else {
        m_min = m_default;
        m_max = m_default;
    }
}

Tunable& Tunable::range(double min, double max, double step) {
    CONEY_ASSERT(min <= max && step > 0.0);
    m_min = min;
    m_max = max;
    m_step = step;
    return *this;
}

Tunable& Tunable::units(std::string units) {
    m_units = std::move(units);
    return *this;
}

Tunable& Tunable::describe(std::string description) {
    m_description = std::move(description);
    return *this;
}

TunableType Tunable::type() const {
    if (std::holds_alternative<bool*>(m_target)) {
        return TunableType::Bool;
    }
    return std::holds_alternative<int*>(m_target) ? TunableType::Int : TunableType::Float;
}

double Tunable::current() const {
    return std::visit([](auto* target) { return static_cast<double>(*target); }, m_target);
}

double Tunable::clamp(double value) const {
    if (type() != TunableType::Float) {
        value = std::round(value);
    }
    return std::clamp(value, m_min, m_max);
}

std::string Tunable::format(double value) const {
    std::string text;
    switch (type()) {
    case TunableType::Bool:
        return value != 0.0 ? "on" : "off";
    case TunableType::Int:
        text = std::format("{}", static_cast<long long>(std::llround(value)));
        break;
    case TunableType::Float:
        // Up to three decimals, without trailing zeros: 0.25, 1.5, 3.
        text = std::format("{:.3f}", value);
        while (text.ends_with('0')) {
            text.pop_back();
        }
        if (text.ends_with('.')) {
            text.pop_back();
        }
        if (text == "-0") {
            text = "0";
        }
        break;
    }
    return m_units.empty() ? text : text + " " + m_units;
}

void Tunable::write(double value) {
    const double clamped = clamp(value);
    std::visit(
        [clamped](auto* target) {
            using T = std::remove_pointer_t<decltype(target)>;
            if constexpr (std::is_same_v<T, bool>) {
                *target = clamped != 0.0;
            } else {
                *target = static_cast<T>(clamped);
            }
        },
        m_target);
}

Tunable& TunableRegistry::add(std::string category, std::string name, bool* target) {
    return addTarget(std::move(category), std::move(name), target);
}

Tunable& TunableRegistry::add(std::string category, std::string name, int* target) {
    return addTarget(std::move(category), std::move(name), target);
}

Tunable& TunableRegistry::add(std::string category, std::string name, float* target) {
    return addTarget(std::move(category), std::move(name), target);
}

Tunable& TunableRegistry::addTarget(std::string category, std::string name, Tunable::Target target) {
    CONEY_ASSERT(std::visit([](auto* pointer) { return pointer != nullptr; }, target));
    // A path is the key of the file and of pins, so it must be unique and must not hold the file's separators.
    CONEY_ASSERT(category.find_first_of("/=#\n") == std::string::npos &&
                 name.find_first_of("=#\n") == std::string::npos);
    const std::string path = category + "/" + name;
    CONEY_ASSERT(!indexOf(path));
    m_tunables.push_back(std::unique_ptr<Tunable>(new Tunable(std::move(category), std::move(name), target)));
    // An override read before the subsystem registered waits for the next applyPending(), like any other change.
    if (const auto waiting = m_unclaimed.find(path); waiting != m_unclaimed.end()) {
        m_pending[path] = waiting->second; // clamped when written: the range comes after add()
        m_unclaimed.erase(waiting);
    }
    return *m_tunables.back();
}

std::optional<std::size_t> TunableRegistry::indexOf(std::string_view path) const {
    for (std::size_t i = 0; i < m_tunables.size(); ++i) {
        if (m_tunables[i]->path() == path) {
            return i;
        }
    }
    return std::nullopt;
}

void TunableRegistry::remove(std::string_view path) {
    if (const auto index = indexOf(path)) {
        m_tunables.erase(m_tunables.begin() + static_cast<std::ptrdiff_t>(*index));
    }
    if (const auto queued = m_pending.find(path); queued != m_pending.end()) {
        m_pending.erase(queued);
    }
}

std::size_t TunableRegistry::removeCategory(std::string_view category) {
    std::vector<std::string> paths;
    for (const auto& tunable : m_tunables) {
        if (tunable->category() == category) {
            paths.push_back(tunable->path());
        }
    }
    for (const std::string& path : paths) {
        remove(path);
    }
    return paths.size();
}

Tunable* TunableRegistry::find(std::string_view path) {
    const auto index = indexOf(path);
    return index ? m_tunables[*index].get() : nullptr;
}

std::vector<std::string> TunableRegistry::categories() const {
    std::set<std::string> names;
    for (const auto& tunable : m_tunables) {
        names.insert(tunable->category());
    }
    return {names.begin(), names.end()};
}

std::vector<Tunable*> TunableRegistry::inCategory(std::string_view category) {
    std::vector<Tunable*> result;
    for (const auto& tunable : m_tunables) {
        if (tunable->category() == category) {
            result.push_back(tunable.get());
        }
    }
    return result;
}

bool TunableRegistry::set(std::string_view path, double value) {
    Tunable* tunable = find(path);
    if (tunable == nullptr) {
        return false;
    }
    m_pending[std::string(path)] = tunable->clamp(value);
    return true;
}

void TunableRegistry::resetAll() {
    for (const auto& tunable : m_tunables) {
        m_pending[tunable->path()] = tunable->defaultValue();
    }
}

std::optional<double> TunableRegistry::value(std::string_view path) const {
    const auto index = indexOf(path);
    if (!index) {
        return std::nullopt;
    }
    if (const auto queued = m_pending.find(path); queued != m_pending.end()) {
        return m_tunables[*index]->clamp(queued->second);
    }
    return m_tunables[*index]->current();
}

std::size_t TunableRegistry::applyPending() {
    const std::size_t count = m_pending.size();
    for (const auto& [path, value] : m_pending) {
        if (Tunable* tunable = find(path); tunable != nullptr) {
            tunable->write(value);
        }
    }
    m_pending.clear();
    return count;
}

std::string TunableRegistry::saveText() const {
    // The file's lines by path: the registered tunables away from their default, then the overrides still waiting.
    std::map<std::string, std::string> lines;
    for (const auto& tunable : m_tunables) {
        const double value = this->value(tunable->path()).value_or(tunable->current());
        if (value != tunable->defaultValue()) {
            lines[tunable->path()] = std::format("{:.9g}", value);
        }
    }
    for (const auto& [path, value] : m_unclaimed) {
        lines.try_emplace(path, std::format("{:.9g}", value));
    }
    std::string text = "# Coney tunable overrides (docs/guides/debug-menu.md#tunables): category/name = value\n";
    for (const auto& [path, value] : lines) {
        text += std::format("{} = {}\n", path, value);
    }
    return text;
}

std::expected<std::size_t, Error> TunableRegistry::loadText(std::string_view text) {
    // Parse every line first, so a bad file changes nothing.
    std::vector<std::pair<std::string, double>> overrides;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto equals = line.find('=');
        const std::string_view path = equals == std::string_view::npos ? line : trim(line.substr(0, equals));
        const auto value = equals == std::string_view::npos ? std::nullopt : parseValue(trim(line.substr(equals + 1)));
        if (!value || path.find('/') == std::string_view::npos) {
            return fail(
                ErrorCode::Invalid,
                std::format("tunables line {}: expected `category/name = value`, got \"{}\"", lineNumber, line));
        }
        overrides.emplace_back(std::string(path), *value);
    }
    // Then queue them, or keep them for a tunable that registers later.
    for (const auto& [path, value] : overrides) {
        if (!set(path, value)) {
            m_unclaimed[path] = value;
        }
    }
    return overrides.size();
}

std::expected<void, Error> TunableRegistry::save(const std::string& path) const {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string text = saveText();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        return fail(ErrorCode::Io, std::format("{}: cannot write the tunables file", path));
    }
    return {};
}

std::expected<std::size_t, Error> TunableRegistry::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("{}: no such tunables file", path));
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return loadText(text);
}

TunableRegistry& globalTunables() {
    static TunableRegistry registry;
    return registry;
}

} // namespace coney::debug
