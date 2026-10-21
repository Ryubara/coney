// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/json.h"

#include <charconv>
#include <format>
#include <system_error>

#include "core/parse_number.h"

namespace coney::json {

namespace {

// Deeper documents are refused: nothing Coney reads nests more than a few levels, and the parser recurses.
constexpr int kMaxDepth = 64;

// A recursive-descent reader over the text; `pos` is the next unread byte.
class Reader {
  public:
    explicit Reader(std::string_view text) : m_text(text) {}

    // The whole document: one value, then nothing but whitespace.
    std::expected<Value, Error> document() {
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the shared_ptr in a nested array or object owns it
        auto value = parseValue(0);
        if (!value) {
            return value;
        }
        skipSpace();
        if (m_pos != m_text.size()) {
            return failAt("text after the value");
        }
        return value;
    }

  private:
    // An error naming the byte it happened at.
    std::unexpected<Error> failAt(std::string_view what) const {
        return fail(ErrorCode::Invalid, std::format("JSON: {} at byte {}", what, m_pos));
    }

    // Skips the four whitespace characters JSON allows.
    void skipSpace() {
        while (m_pos < m_text.size() &&
               (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' || m_text[m_pos] == '\n' || m_text[m_pos] == '\r')) {
            ++m_pos;
        }
    }

    // Takes `word` if the text continues with it.
    bool take(std::string_view word) {
        if (m_text.substr(m_pos).starts_with(word)) {
            m_pos += word.size();
            return true;
        }
        return false;
    }

    // Any value, by its first character.
    std::expected<Value, Error> parseValue(int depth) {
        skipSpace();
        if (m_pos == m_text.size()) {
            return failAt("a value is missing");
        }
        const char c = m_text[m_pos];
        if (c == '{' || c == '[') {
            if (depth >= kMaxDepth) {
                return failAt("nesting too deep");
            }
            return c == '{' ? parseObject(depth + 1) : parseArray(depth + 1);
        }
        if (c == '"') {
            auto text = parseString();
            if (!text) {
                return std::unexpected(std::move(text.error()));
            }
            return Value(std::move(*text));
        }
        if (take("true")) {
            return Value(true);
        }
        if (take("false")) {
            return Value(false);
        }
        if (take("null")) {
            return Value();
        }
        return parseNumber();
    }

    // `{ "key": value, ... }`.
    std::expected<Value, Error> parseObject(int depth) {
        ++m_pos;
        Object object;
        skipSpace();
        if (take("}")) {
            return Value(std::move(object));
        }
        while (true) {
            skipSpace();
            if (m_pos == m_text.size() || m_text[m_pos] != '"') {
                return failAt("an object key must be a string");
            }
            auto key = parseString();
            if (!key) {
                return std::unexpected(std::move(key.error()));
            }
            skipSpace();
            if (!take(":")) {
                return failAt("':' expected");
            }
            auto value = parseValue(depth);
            if (!value) {
                return value;
            }
            if (!object.emplace(std::move(*key), std::move(*value)).second) {
                return failAt("a key repeated in an object");
            }
            skipSpace();
            if (take("}")) {
                return Value(std::move(object));
            }
            if (!take(",")) {
                return failAt("',' or '}' expected");
            }
        }
    }

    // `[ value, ... ]`.
    std::expected<Value, Error> parseArray(int depth) {
        ++m_pos;
        Array array;
        skipSpace();
        if (take("]")) {
            return Value(std::move(array));
        }
        while (true) {
            auto value = parseValue(depth);
            if (!value) {
                return value;
            }
            array.push_back(std::move(*value));
            skipSpace();
            if (take("]")) {
                return Value(std::move(array));
            }
            if (!take(",")) {
                return failAt("',' or ']' expected");
            }
        }
    }

    // Four hex digits of a `\u` escape.
    std::expected<std::uint32_t, Error> hex4() {
        if (m_text.size() - m_pos < 4) {
            return failAt("a \\u escape needs four hex digits");
        }
        std::uint32_t value = 0;
        const auto [end, ec] = std::from_chars(m_text.data() + m_pos, m_text.data() + m_pos + 4, value, 16);
        if (ec != std::errc{} || end != m_text.data() + m_pos + 4) {
            return failAt("a \\u escape needs four hex digits");
        }
        m_pos += 4;
        return value;
    }

    // Appends code point `cp` as UTF-8.
    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    // A string, from its opening quote; escapes decoded, `\u` pairs joined into one code point.
    std::expected<std::string, Error> parseString() {
        ++m_pos;
        std::string out;
        while (true) {
            if (m_pos == m_text.size()) {
                return failAt("a string is not closed");
            }
            const char c = m_text[m_pos++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return failAt("a control character in a string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_pos == m_text.size()) {
                return failAt("a string is not closed");
            }
            const char e = m_text[m_pos++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out.push_back(e);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                auto cp = hex4();
                if (!cp) {
                    return std::unexpected(std::move(cp.error()));
                }
                std::uint32_t code = *cp;
                if (code >= 0xDC00 && code <= 0xDFFF) {
                    return failAt("a lone low surrogate");
                }
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (!take("\\u")) {
                        return failAt("a high surrogate without its low half");
                    }
                    auto low = hex4();
                    if (!low) {
                        return std::unexpected(std::move(low.error()));
                    }
                    if (*low < 0xDC00 || *low > 0xDFFF) {
                        return failAt("a high surrogate without its low half");
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (*low - 0xDC00);
                }
                appendUtf8(out, code);
                break;
            }
            default:
                return failAt("an unknown escape");
            }
        }
    }

    // A number as RFC 8259 writes it: `-`, digits without a leading zero, then a fraction and an exponent.
    std::expected<Value, Error> parseNumber() {
        const std::size_t start = m_pos;
        const auto digits = [&] {
            const std::size_t first = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
            }
            return m_pos - first;
        };
        take("-");
        const std::size_t intStart = m_pos;
        const std::size_t intDigits = digits();
        if (intDigits == 0) {
            m_pos = start;
            return failAt("not a JSON value");
        }
        if (intDigits > 1 && m_text[intStart] == '0') {
            return failAt("a number with a leading zero");
        }
        bool whole = true;
        if (take(".")) {
            whole = false;
            if (digits() == 0) {
                return failAt("a fraction needs digits");
            }
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            whole = false;
            ++m_pos;
            if (!take("+")) {
                take("-");
            }
            if (digits() == 0) {
                return failAt("an exponent needs digits");
            }
        }
        const std::string_view text = m_text.substr(start, m_pos - start);
        // parseDecimal, not from_chars for doubles, which some supported standard libraries lack.
        const std::optional<double> value = parseDecimal(text);
        if (!value) {
            return failAt("a number out of range");
        }
        std::optional<std::int64_t> integer;
        if (whole) {
            std::int64_t parsed = 0;
            if (const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
                ec == std::errc{} && end == text.data() + text.size()) {
                integer = parsed;
            }
        }
        return Value::number(*value, integer);
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
};

} // namespace

Value Value::number(double value, std::optional<std::int64_t> integer) {
    Value number;
    number.m_data = Number{value, integer};
    return number;
}

std::optional<bool> Value::asBool() const {
    const bool* value = std::get_if<bool>(&m_data);
    return value != nullptr ? std::optional<bool>(*value) : std::nullopt;
}

std::optional<double> Value::asDouble() const {
    const Number* value = std::get_if<Number>(&m_data);
    return value != nullptr ? std::optional<double>(value->value) : std::nullopt;
}

std::optional<std::int64_t> Value::asInteger() const {
    const Number* value = std::get_if<Number>(&m_data);
    return value != nullptr ? value->integer : std::nullopt;
}

const std::string* Value::asString() const { return std::get_if<std::string>(&m_data); }

const Array* Value::asArray() const {
    const auto* value = std::get_if<std::shared_ptr<Array>>(&m_data);
    return value != nullptr ? value->get() : nullptr;
}

const Object* Value::asObject() const {
    const auto* value = std::get_if<std::shared_ptr<Object>>(&m_data);
    return value != nullptr ? value->get() : nullptr;
}

const Value* Value::find(std::string_view key) const {
    const Object* object = asObject();
    if (object == nullptr) {
        return nullptr;
    }
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

std::expected<Value, Error> parse(std::string_view text) { return Reader(text).document(); }

} // namespace coney::json
