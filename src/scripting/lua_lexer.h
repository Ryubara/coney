// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The lexer of Coney's Lua 4.0 compiler: source text to tokens, with the line of each, as the Lua 4.0 reference
// manual's lexical conventions define them.
// @coney-only part of Coney's own compiler (lua_compiler.h)

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "core/error.h"

namespace coney::script {

/// What a token is. Reserved words and symbols each have their own kind.
enum class LuaToken : std::uint8_t {
    // Reserved words (Lua 4.0 has no `true`, `false` or `local function`).
    And,
    Break,
    Do,
    Else,
    Elseif,
    End,
    For,
    Function,
    If,
    In,
    Local,
    Nil,
    Not,
    Or,
    Repeat,
    Return,
    Then,
    Until,
    While,
    // Symbols of more than one character.
    Concat,    // ..
    Dots,      // ...
    Equal,     // ==
    GreaterEq, // >=
    LessEq,    // <=
    NotEqual,  // ~=
    // Symbols of one character.
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    Percent,
    Assign,
    Less,
    Greater,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Semicolon,
    Comma,
    Dot,
    Colon,
    // Tokens with a value.
    Name,
    Number,
    String,
    EndOfSource,
};

/// Cuts Lua 4.0 source into tokens, one at a time, keeping the current token and the one after it.
class LuaLexer {
  public:
    /// A lexer over `source`, whose errors name the chunk as `label` (luaChunkLabel()). Call start() before reading.
    LuaLexer(std::string_view source, std::string label);

    /// Reads the first two tokens. Fails as advance() does.
    [[nodiscard]] std::expected<void, Error> start();
    /// Moves to the next token. Fails with ErrorCode::Invalid on malformed input (an unfinished string, a malformed
    /// number, a character Lua has no use for).
    [[nodiscard]] std::expected<void, Error> advance();

    /// The current token's kind.
    [[nodiscard]] LuaToken token() const { return m_current.kind; }
    /// The current token's text: the name, or the string's contents (raw bytes).
    [[nodiscard]] const std::string& text() const { return m_current.text; }
    /// The current token's value, for a number.
    [[nodiscard]] double number() const { return m_current.number; }
    /// The line the current token is on.
    [[nodiscard]] int line() const { return m_current.line; }
    /// The kind of the token after the current one.
    [[nodiscard]] LuaToken lookahead() const { return m_next.kind; }
    /// The line of the token before the current one: where the code just compiled ends.
    [[nodiscard]] int lastLine() const { return m_lastLine; }

    /// An error at the current token: `<chunk>:<line>: <what> near '<token>'`.
    [[nodiscard]] Error error(std::string_view what) const;
    /// An error at `line`, without naming a token.
    [[nodiscard]] Error errorAt(int line, std::string_view what) const;

    /// How a token kind is written in messages: `end`, `==`, `<name>`.
    [[nodiscard]] static std::string_view spelling(LuaToken kind);

  private:
    // One token as read.
    struct Token {
        LuaToken kind = LuaToken::EndOfSource;
        std::string text;
        double number = 0.0;
        int line = 1;
    };

    // Reads the token at the read position into `out`.
    std::expected<void, Error> read(Token& out);
    // Reads a number that starts at the read position.
    std::expected<void, Error> readNumber(Token& out);
    // Reads a string closed by `quote`, with its escapes.
    std::expected<void, Error> readString(Token& out, char quote);
    // Reads a `[[ ... ]]` string, which may hold nested pairs and spans lines.
    std::expected<void, Error> readLongString(Token& out);
    // The character `ahead` places past the read position, or 0 past the end.
    [[nodiscard]] char peek(std::size_t ahead = 0) const;
    // An error on the line being read, quoting `near`.
    [[nodiscard]] std::unexpected<Error> failNear(std::string_view what, std::string_view near) const;

    std::string_view m_source;
    std::string m_label;
    std::size_t m_at = 0; // read position
    int m_readLine = 1;   // the line at the read position
    Token m_current;
    Token m_next;
    int m_lastLine = 1;
};

} // namespace coney::script
