// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only part of Coney's own compiler (lua_compiler.h)
#include "scripting/lua_lexer.h"

#include <array>
#include <format>
#include <optional>
#include <utility>

#include "scripting/lua_vm.h"

namespace coney::script {

namespace {

// The reserved words, in the order of LuaToken's first entries.
constexpr std::array<std::string_view, 19> kReserved{"and",      "break",  "do",   "else",  "elseif", "end", "for",
                                                     "function", "if",     "in",   "local", "nil",    "not", "or",
                                                     "repeat",   "return", "then", "until", "while"};

// The one-character symbols, in the order of LuaToken's entries from Plus.
constexpr std::string_view kSingleSymbols = "+-*/^%=<>(){}[];,.:";

// The longest string or name a token may hold: past this the source is not a script.
constexpr std::size_t kMaxTokenLength = 1U << 24;

// True for a character that may start a name.
bool startsName(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
// True for a decimal digit.
bool isDigit(char c) { return c >= '0' && c <= '9'; }
// True for a character that may continue a name.
bool continuesName(char c) { return startsName(c) || isDigit(c); }

} // namespace

LuaLexer::LuaLexer(std::string_view source, std::string label) : m_source(source), m_label(std::move(label)) {}

std::expected<void, Error> LuaLexer::start() {
    if (auto first = read(m_current); !first) {
        return first;
    }
    return read(m_next);
}

std::expected<void, Error> LuaLexer::advance() {
    m_lastLine = m_current.line;
    m_current = std::move(m_next);
    m_next = Token{};
    return read(m_next);
}

Error LuaLexer::error(std::string_view what) const {
    std::string near;
    switch (m_current.kind) {
    case LuaToken::Name:
    case LuaToken::String:
        near = m_current.text;
        break;
    case LuaToken::Number:
        near = std::format("{:.14g}", m_current.number);
        break;
    default:
        near = std::string(spelling(m_current.kind));
        break;
    }
    return Error{ErrorCode::Invalid, std::format("{}:{}: {} near '{}'", m_label, m_current.line, what, near)};
}

Error LuaLexer::errorAt(int line, std::string_view what) const {
    return Error{ErrorCode::Invalid, std::format("{}:{}: {}", m_label, line, what)};
}

std::string_view LuaLexer::spelling(LuaToken kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (index < kReserved.size()) {
        return kReserved[index];
    }
    switch (kind) {
    case LuaToken::Concat:
        return "..";
    case LuaToken::Dots:
        return "...";
    case LuaToken::Equal:
        return "==";
    case LuaToken::GreaterEq:
        return ">=";
    case LuaToken::LessEq:
        return "<=";
    case LuaToken::NotEqual:
        return "~=";
    case LuaToken::Name:
        return "<name>";
    case LuaToken::Number:
        return "<number>";
    case LuaToken::String:
        return "<string>";
    case LuaToken::EndOfSource:
        return "<eof>";
    default: {
        const std::size_t symbol = index - static_cast<std::size_t>(LuaToken::Plus);
        return symbol < kSingleSymbols.size() ? kSingleSymbols.substr(symbol, 1) : "?";
    }
    }
}

char LuaLexer::peek(std::size_t ahead) const { return m_at + ahead < m_source.size() ? m_source[m_at + ahead] : '\0'; }

std::unexpected<Error> LuaLexer::failNear(std::string_view what, std::string_view near) const {
    return std::unexpected(
        Error{ErrorCode::Invalid, std::format("{}:{}: {} near '{}'", m_label, m_readLine, what, near)});
}

std::expected<void, Error> LuaLexer::read(Token& out) {
    while (true) {
        // Spaces, new lines and comments separate tokens; a comment runs from `--` to the end of the line.
        while (m_at < m_source.size()) {
            const char c = m_source[m_at];
            if (c == '\n') {
                ++m_readLine;
                ++m_at;
            } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
                ++m_at;
            } else if (c == '-' && peek(1) == '-') {
                while (m_at < m_source.size() && m_source[m_at] != '\n') {
                    ++m_at;
                }
            } else {
                break;
            }
        }
        out.line = m_readLine;
        if (m_at >= m_source.size()) {
            out.kind = LuaToken::EndOfSource;
            return {};
        }
        const char c = m_source[m_at];
        if (startsName(c)) {
            const std::size_t begin = m_at;
            while (continuesName(peek())) {
                ++m_at;
            }
            out.text = std::string(m_source.substr(begin, m_at - begin));
            out.kind = LuaToken::Name;
            for (std::size_t i = 0; i < kReserved.size(); ++i) {
                if (kReserved[i] == out.text) {
                    out.kind = static_cast<LuaToken>(i);
                    break;
                }
            }
            return {};
        }
        if (isDigit(c) || (c == '.' && isDigit(peek(1)))) {
            return readNumber(out);
        }
        if (c == '"' || c == '\'') {
            return readString(out, c);
        }
        if (c == '[' && peek(1) == '[') {
            return readLongString(out);
        }
        // Symbols: the longest that matches.
        const auto two = [&](char second, LuaToken kind) {
            if (peek(1) != second) {
                return false;
            }
            out.kind = kind;
            m_at += 2;
            return true;
        };
        if (c == '.' && peek(1) == '.') {
            out.kind = peek(2) == '.' ? LuaToken::Dots : LuaToken::Concat;
            m_at += out.kind == LuaToken::Dots ? 3 : 2;
            return {};
        }
        if ((c == '=' && two('=', LuaToken::Equal)) || (c == '<' && two('=', LuaToken::LessEq)) ||
            (c == '>' && two('=', LuaToken::GreaterEq)) || (c == '~' && two('=', LuaToken::NotEqual))) {
            return {};
        }
        if (const std::size_t symbol = kSingleSymbols.find(c); symbol != std::string_view::npos) {
            out.kind = static_cast<LuaToken>(static_cast<std::size_t>(LuaToken::Plus) + symbol);
            ++m_at;
            return {};
        }
        return failNear("unexpected character", std::string(1, c));
    }
}

std::expected<void, Error> LuaLexer::readNumber(Token& out) {
    // digits, then an optional fraction, then an optional exponent: 12, 3., .5, 1.5e-3.
    const std::size_t begin = m_at;
    while (isDigit(peek())) {
        ++m_at;
    }
    if (peek() == '.') {
        ++m_at;
        if (peek() == '.') {
            // `3..x` could be the number `3.` and `.x`, or 3 joined to x: Lua refuses to guess.
            ++m_at;
            return failNear("ambiguous syntax (a decimal point or a concatenation)",
                            m_source.substr(begin, m_at - begin));
        }
        while (isDigit(peek())) {
            ++m_at;
        }
    }
    if (peek() == 'e' || peek() == 'E') {
        ++m_at;
        if (peek() == '+' || peek() == '-') {
            ++m_at;
        }
        if (!isDigit(peek())) {
            return failNear("malformed number", m_source.substr(begin, m_at - begin));
        }
        while (isDigit(peek())) {
            ++m_at;
        }
    }
    const std::string_view text = m_source.substr(begin, m_at - begin);
    const std::optional<double> value = parseLuaNumber(text);
    if (!value) {
        return failNear("malformed number", text);
    }
    out.kind = LuaToken::Number;
    out.number = *value;
    return {};
}

std::expected<void, Error> LuaLexer::readString(Token& out, char quote) {
    const int firstLine = m_readLine;
    ++m_at; // the opening quote
    std::string text;
    while (true) {
        if (m_at >= m_source.size() || peek() == '\n') {
            m_readLine = firstLine;
            return failNear("unfinished string", std::string(1, quote) + text);
        }
        if (text.size() > kMaxTokenLength) {
            return failNear("string too long", std::string(1, quote));
        }
        const char c = m_source[m_at++];
        if (c == quote) {
            break;
        }
        if (c != '\\') {
            text.push_back(c);
            continue;
        }
        // An escape: the C ones, a backslash before a new line (a new line), or up to three decimal digits.
        const char e = peek();
        ++m_at;
        switch (e) {
        case 'a':
            text.push_back('\a');
            break;
        case 'b':
            text.push_back('\b');
            break;
        case 'f':
            text.push_back('\f');
            break;
        case 'n':
            text.push_back('\n');
            break;
        case 'r':
            text.push_back('\r');
            break;
        case 't':
            text.push_back('\t');
            break;
        case 'v':
            text.push_back('\v');
            break;
        case '\n':
            ++m_readLine;
            text.push_back('\n');
            break;
        case '\0':
            if (m_at > m_source.size()) {
                m_readLine = firstLine;
                return failNear("unfinished string", std::string(1, quote) + text);
            }
            text.push_back('\0');
            break;
        default:
            if (isDigit(e)) {
                int code = e - '0';
                for (int digits = 1; digits < 3 && isDigit(peek()); ++digits) {
                    code = (code * 10) + (peek() - '0');
                    ++m_at;
                }
                if (code > 255) {
                    return failNear("escape sequence too large", std::string(1, quote) + text);
                }
                text.push_back(static_cast<char>(code));
            } else {
                text.push_back(e); // any other character stands for itself: \\, \", \'
            }
            break;
        }
    }
    out.kind = LuaToken::String;
    out.text = std::move(text);
    return {};
}

std::expected<void, Error> LuaLexer::readLongString(Token& out) {
    const int firstLine = m_readLine;
    m_at += 2; // [[
    // A new line straight after the opening brackets is not part of the string.
    if (peek() == '\n') {
        ++m_readLine;
        ++m_at;
    }
    std::string text;
    int depth = 1;
    while (true) {
        if (m_at >= m_source.size()) {
            m_readLine = firstLine;
            return failNear("unfinished long string", "[[");
        }
        if (text.size() > kMaxTokenLength) {
            return failNear("string too long", "[[");
        }
        const char c = m_source[m_at];
        if (c == '[' && peek(1) == '[') {
            ++depth;
            text += "[[";
            m_at += 2;
        } else if (c == ']' && peek(1) == ']') {
            m_at += 2;
            if (--depth == 0) {
                break;
            }
            text += "]]";
        } else {
            if (c == '\n') {
                ++m_readLine;
            }
            text.push_back(c);
            ++m_at;
        }
    }
    out.kind = LuaToken::String;
    out.text = std::move(text);
    return {};
}

} // namespace coney::script
