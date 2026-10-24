// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/error.h"

namespace coney::json {

class Value;

/// A JSON array.
using Array = std::vector<Value>;
/// A JSON object; keys are kept sorted, and a repeated key is refused when parsing.
using Object = std::map<std::string, Value, std::less<>>;

/// A JSON value (RFC 8259): null, a boolean, a number, a string, an array or an object.
///
/// Coney reads its own small files with it: an install's `index/install.json` and the launcher's release manifest.
/// Numbers keep their text's integer value when it has one, so sizes up to 2^63 survive exactly.
class Value {
  public:
    /// null.
    Value() = default;
    /// Wraps a boolean, a string, an array or an object.
    explicit Value(bool value) : m_data(value) {}
    explicit Value(std::string value) : m_data(std::move(value)) {}
    /// Without this, a string literal would pick the boolean constructor.
    explicit Value(const char* value) : m_data(std::string(value)) {}
    explicit Value(Array value) : m_data(std::make_shared<Array>(std::move(value))) {}
    explicit Value(Object value) : m_data(std::make_shared<Object>(std::move(value))) {}
    /// A number: its value as a double, and as an integer when the text was a whole number that fits.
    static Value number(double value, std::optional<std::int64_t> integer);

    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::monostate>(m_data); }
    /// The boolean, or nullopt for another kind of value.
    [[nodiscard]] std::optional<bool> asBool() const;
    /// The number as a double, or nullopt.
    [[nodiscard]] std::optional<double> asDouble() const;
    /// The number as an integer when it was written as one (no fraction or exponent) and fits, or nullopt.
    [[nodiscard]] std::optional<std::int64_t> asInteger() const;
    /// The string, or nullptr.
    [[nodiscard]] const std::string* asString() const;
    /// The array, or nullptr.
    [[nodiscard]] const Array* asArray() const;
    /// The object, or nullptr.
    [[nodiscard]] const Object* asObject() const;
    /// The member `key` of an object, or nullptr when this is not an object or has no such member.
    [[nodiscard]] const Value* find(std::string_view key) const;

  private:
    /// A number keeps both readings.
    struct Number {
        double value = 0;
        std::optional<std::int64_t> integer;
    };

    // Arrays and objects are shared so a Value stays cheap to copy and its type complete at this point.
    std::variant<std::monostate, bool, Number, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> m_data;
};

/// Parses one JSON document. Fails with ErrorCode::Invalid and the byte offset for anything RFC 8259 refuses
/// (trailing commas, comments, single quotes, a lone surrogate, a string that is not UTF-8, text after the value), a
/// repeated object key, or nesting deeper than 64 levels.
[[nodiscard]] std::expected<Value, Error> parse(std::string_view text);

} // namespace coney::json
