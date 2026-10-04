// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lua_value.h"

#include <cmath>
#include <utility>

namespace coney::script {

namespace {

// Shared empty handles for the accessors that return a reference.
const std::shared_ptr<Table> kNoTable;
const std::shared_ptr<const Function> kNoFunction;

} // namespace

Value::Value(std::shared_ptr<Table> table) {
    if (table) {
        m_value = std::move(table);
    }
}

Value::Value(std::shared_ptr<const Function> function) {
    if (function) {
        m_value = std::move(function);
    }
}

std::optional<double> Value::number() const {
    if (const double* number = std::get_if<double>(&m_value)) {
        return *number;
    }
    return std::nullopt;
}

std::optional<std::string_view> Value::string() const {
    if (const std::string* text = std::get_if<std::string>(&m_value)) {
        return std::string_view(*text);
    }
    return std::nullopt;
}

const std::shared_ptr<Table>& Value::table() const {
    if (const auto* table = std::get_if<std::shared_ptr<Table>>(&m_value)) {
        return *table;
    }
    return kNoTable;
}

const std::shared_ptr<const Function>& Value::function() const {
    if (const auto* function = std::get_if<std::shared_ptr<const Function>>(&m_value)) {
        return *function;
    }
    return kNoFunction;
}

std::optional<Table::Key> Table::keyOf(const Value& key) {
    switch (key.type()) {
    case Value::Type::Nil:
        return std::nullopt;
    case Value::Type::Number: {
        const double number = key.number().value_or(0.0);
        // -0 and 0 are the same key; the hash of a double need not agree.
        return Key(number == 0.0 ? 0.0 : number);
    }
    case Value::Type::String:
        return Key(std::string(key.string().value_or(std::string_view{})));
    case Value::Type::Table:
        return Key(static_cast<const void*>(key.table().get()));
    case Value::Type::Function:
        return Key(static_cast<const void*>(key.function().get()));
    }
    return std::nullopt;
}

Value Table::get(const Value& key) const {
    const std::optional<Key> found = keyOf(key);
    if (!found) {
        return {};
    }
    const auto it = m_index.find(*found);
    return it == m_index.end() ? Value() : m_entries[it->second].second;
}

std::expected<void, Error> Table::set(const Value& key, Value value) {
    std::optional<Key> found = keyOf(key);
    if (!found) {
        return fail(ErrorCode::Invalid, "Lua: table index is nil");
    }
    if (const double* number = std::get_if<double>(&*found); number != nullptr && std::isnan(*number)) {
        return fail(ErrorCode::Invalid, "Lua: table index is NaN");
    }
    if (const auto it = m_index.find(*found); it != m_index.end()) {
        m_entries[it->second].second = std::move(value);
        return {};
    }
    // A new key set to nil adds nothing.
    if (value.isNil()) {
        return {};
    }
    m_index.emplace(std::move(*found), m_entries.size());
    m_entries.emplace_back(key, std::move(value));
    return {};
}

std::optional<std::pair<Value, Value>> Table::next(const Value& key) const {
    std::size_t position = 0;
    if (!key.isNil()) {
        const std::optional<Key> found = keyOf(key);
        const auto it = found ? m_index.find(*found) : m_index.end();
        if (it == m_index.end()) {
            return std::nullopt;
        }
        position = it->second + 1;
    }
    // Skip entries that were set to nil after they were added.
    for (; position < m_entries.size(); ++position) {
        if (!m_entries[position].second.isNil()) {
            return m_entries[position];
        }
    }
    return std::nullopt;
}

std::size_t Table::size() const {
    std::size_t count = 0;
    for (const auto& entry : m_entries) {
        count += entry.second.isNil() ? 0 : 1;
    }
    return count;
}

} // namespace coney::script
