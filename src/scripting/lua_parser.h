// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The parser of Coney's Lua 4.0 compiler: a recursive descent over the Lua 4.0 reference manual's grammar that emits
// each function's code as it reads it, in one pass, as Lua 4.0's own compiler does. Statements and functions are in
// lua_parser.cpp, expressions in lua_expressions.cpp; compileLua() (lua_compiler.h) is the way in.
// @coney-only part of Coney's own compiler (lua_compiler.h)

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "scripting/lua_chunk.h"
#include "scripting/lua_code.h"
#include "scripting/lua_lexer.h"

namespace coney::script {

/// Reads one chunk of Lua 4.0 source and writes its functions.
class LuaParser {
  public:
    /// A parser over `source`, whose functions take `chunkName` as their source and whose errors name `label`.
    LuaParser(std::string_view source, std::string chunkName, std::string label);

    /// Compiles the whole chunk into its main function. Fails with the first error.
    [[nodiscard]] std::expected<LuaProto, Error> compile();

  private:
    // What an expression is while it is compiled: where its value is, and, for a condition, the jumps it exits by.
    struct Expr {
        enum class Kind : std::uint8_t {
            Global,  // the global named by string constant `index`, not read yet
            Local,   // local slot `index`, not read yet
            Indexed, // a table (in slot `index`) and a key on the stack, not indexed yet
            Call,    // the CALL at position `index`, keeping all its results until told otherwise
            Value,   // on the stack (or about to be, through the jumps below)
        };
        Kind kind = Kind::Value;
        std::uint32_t index = 0;
        LuaJumpList whenTrue;  // jumps taken when the expression is true (not nil)
        LuaJumpList whenFalse; // jumps taken when it is false (nil)
        int test = -1;         // the conditional jump the expression ends with, taken when it is true; -1 for none
    };

    // A value a closure copies when it is made (`%name`): a local of the enclosing function, or a global.
    struct Upvalue {
        std::string name;
        bool isLocal = false;
        std::uint32_t slot = 0; // the enclosing function's local slot
    };

    // A loop `break` may leave.
    struct Loop {
        int entryLevel = 0; // the stack count where the loop starts: a break drops what is above it
        LuaJumpList breaks;
    };

    // One function being compiled, with the one around it.
    struct Function {
        Function* parent = nullptr;
        LuaCode code;
        std::vector<std::string> locals; // the active locals, by slot
        std::vector<Upvalue> upvalues;
        std::vector<Loop*> loops; // innermost last
        explicit Function(int fixedSlots) : code(fixedSlots) {}
    };

    // ---- Tokens and errors (lua_parser.cpp) ----
    bool next();
    bool accept(LuaToken token);
    bool expect(LuaToken token);
    bool expectMatch(LuaToken closing, LuaToken opening, int openedAt);
    bool expectName(std::string& name);
    bool failHere(std::string_view what);
    bool failAt(int at, std::string_view what);
    [[nodiscard]] bool blockFollows() const;
    [[nodiscard]] int line() const { return m_lex.lastLine(); }
    LuaCode& code() { return m_fs->code; }

    // ---- Statements and functions (lua_parser.cpp) ----
    bool enterLevel();
    bool chunk();
    bool block();
    bool statement(bool& last);
    bool statementAt(bool& last);
    bool ifStatement();
    bool whileStatement();
    bool repeatStatement();
    bool forStatement();
    bool numericFor(const std::string& name, int startLine);
    bool tableFor(const std::string& name, int startLine);
    bool loopBody(Loop& loop, LuaOpcode step, int prepAt, int startLine);
    bool functionStatement();
    bool localStatement();
    bool returnStatement();
    bool breakStatement();
    bool expressionStatement();
    bool assignment(Expr& first);
    void store(const Expr& target);
    bool functionBody(bool method, int startLine, Expr& closure);
    bool addLocal(const std::string& name);
    void dropLocals(std::size_t keep);

    // ---- Expressions (lua_expressions.cpp) ----
    bool expression(Expr& e) { return subexpression(e, 0); }
    bool subexpression(Expr& e, int limit);
    bool subexpressionAt(Expr& e, int limit);
    bool simpleExpression(Expr& e);
    bool primaryExpression(Expr& e);
    bool callArguments(Expr& e, int base);
    bool constructor();
    bool listPart(int& items);
    bool recordPart(int& items);
    bool expressionList(Expr& last, int& count);
    void adjust(int wanted, int count, Expr& last);
    bool variable(const std::string& name, Expr& e);
    bool upvalue(const std::string& name, Expr& e);
    void pushNumber(double value);

    void discharge(Expr& e);
    void dischargeOne(Expr& e);
    void toStack(Expr& e, bool onlyOne);
    void closeCall(Expr& e, int results);
    void goIfTrue(Expr& e);
    void goIfFalse(Expr& e);
    void negate(Expr& e);
    void invertTest(int at);

    LuaLexer m_lex;
    std::string m_chunkName;
    Function* m_fs = nullptr;
    int m_levels = 0; // statements and subexpressions open now, which the C++ stack holds
    std::optional<Error> m_error;
};

} // namespace coney::script
