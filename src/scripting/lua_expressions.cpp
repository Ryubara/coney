// SPDX-License-Identifier: GPL-3.0-or-later
// Expressions of Coney's Lua 4.0 compiler: operators with the reference manual's priorities, variables, calls, table
// constructors, and conditions compiled to jumps. A condition (`and`, `or`, `not`, a comparison) is a set of jumps
// until something needs its value: as a test (`if`, `while`) the jumps go where the code continues, as a value they
// are rewritten to keep the tested value (JMPONT, JMPONF) or to push nil or 1 (PUSHNILJMP, PUSHINT 1), the patterns
// of docs/research/formats/lua-bytecode.md#patterns.
// @coney-only part of Coney's own compiler (lua_compiler.h)
#include "scripting/lua_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <utility>

namespace coney::script {

namespace {

// Lua 4.0 allows this many upvalues per function.
constexpr std::size_t kMaxUpvalues = 32;

// The priority of the unary operators: above the binary ones but `^`.
constexpr int kUnaryPriority = 6;

// A binary operator, with how tightly it binds on its left and on its right (a right-associative operator binds less
// on its right, so a chain groups from the right). From the reference manual's table, lowest first: `and` and `or`;
// the comparisons; `..` (right-associative); `+ -`; `* /`; the unary operators; `^` (right-associative).
struct Binary {
    enum class Kind : std::uint8_t { And, Or, Compare, Arithmetic, Concat };
    Kind kind;
    LuaOpcode op; // the instruction of a comparison or an arithmetic operator
    int left;
    int right;
};

// The binary operator `token` stands for, if any.
std::optional<Binary> binaryOperator(LuaToken token) {
    using Kind = Binary::Kind;
    switch (token) {
    case LuaToken::And:
        return Binary{Kind::And, LuaOpcode::End, 1, 1};
    case LuaToken::Or:
        return Binary{Kind::Or, LuaOpcode::End, 1, 1};
    case LuaToken::Equal:
        return Binary{Kind::Compare, LuaOpcode::JmpEq, 2, 2};
    case LuaToken::NotEqual:
        return Binary{Kind::Compare, LuaOpcode::JmpNe, 2, 2};
    case LuaToken::Less:
        return Binary{Kind::Compare, LuaOpcode::JmpLt, 2, 2};
    case LuaToken::LessEq:
        return Binary{Kind::Compare, LuaOpcode::JmpLe, 2, 2};
    case LuaToken::Greater:
        return Binary{Kind::Compare, LuaOpcode::JmpGt, 2, 2};
    case LuaToken::GreaterEq:
        return Binary{Kind::Compare, LuaOpcode::JmpGe, 2, 2};
    case LuaToken::Concat:
        return Binary{Kind::Concat, LuaOpcode::Concat, 3, 2};
    case LuaToken::Plus:
        return Binary{Kind::Arithmetic, LuaOpcode::Add, 4, 4};
    case LuaToken::Minus:
        return Binary{Kind::Arithmetic, LuaOpcode::Sub, 4, 4};
    case LuaToken::Star:
        return Binary{Kind::Arithmetic, LuaOpcode::Mult, 5, 5};
    case LuaToken::Slash:
        return Binary{Kind::Arithmetic, LuaOpcode::Div, 5, 5};
    case LuaToken::Caret:
        return Binary{Kind::Arithmetic, LuaOpcode::Pow, 7, kUnaryPriority};
    default:
        return std::nullopt;
    }
}

// The test that jumps exactly when `op` does not.
LuaOpcode invertedTest(LuaOpcode op) {
    switch (op) {
    case LuaOpcode::JmpNe:
        return LuaOpcode::JmpEq;
    case LuaOpcode::JmpEq:
        return LuaOpcode::JmpNe;
    case LuaOpcode::JmpLt:
        return LuaOpcode::JmpGe;
    case LuaOpcode::JmpGe:
        return LuaOpcode::JmpLt;
    case LuaOpcode::JmpLe:
        return LuaOpcode::JmpGt;
    case LuaOpcode::JmpGt:
        return LuaOpcode::JmpLe;
    case LuaOpcode::JmpT:
        return LuaOpcode::JmpF;
    case LuaOpcode::JmpF:
        return LuaOpcode::JmpT;
    default:
        return op;
    }
}

// Appends `from` to `to`.
void appendJumps(LuaJumpList& to, const LuaJumpList& from) { to.insert(to.end(), from.begin(), from.end()); }

} // namespace

// An expression whose operators bind more tightly than `limit`, counted as a syntax level.
bool LuaParser::subexpression(Expr& e, int limit) {
    const bool done = enterLevel() && subexpressionAt(e, limit);
    --m_levels;
    return done;
}

// An expression whose operators bind more tightly than `limit`, once its level is counted: unary operators first, then
// binary ones in a loop.
bool LuaParser::subexpressionAt(Expr& e, int limit) {
    if (m_lex.token() == LuaToken::Not || m_lex.token() == LuaToken::Minus) {
        const bool isNot = m_lex.token() == LuaToken::Not;
        if (!next() || !subexpression(e, kUnaryPriority)) {
            return false;
        }
        if (isNot) {
            negate(e);
        } else {
            toStack(e, true);
            code().emit(LuaOpcode::Minus, 0, line());
        }
    } else if (!simpleExpression(e)) {
        return false;
    }
    for (std::optional<Binary> op = binaryOperator(m_lex.token()); op && op->left > limit;
         op = binaryOperator(m_lex.token())) {
        if (!next()) {
            return false;
        }
        // The left operand: a test for `and` and `or`, a value on the stack for the others.
        if (op->kind == Binary::Kind::And) {
            goIfTrue(e);
        } else if (op->kind == Binary::Kind::Or) {
            goIfFalse(e);
        } else {
            toStack(e, true);
        }
        Expr right;
        if (!subexpression(right, op->right)) {
            return false;
        }
        switch (op->kind) {
        case Binary::Kind::And: // false when either is: both operands' false exits
            dischargeOne(right);
            appendJumps(right.whenFalse, e.whenFalse);
            e = std::move(right);
            break;
        case Binary::Kind::Or: // true when either is: both operands' true exits
            dischargeOne(right);
            appendJumps(right.whenTrue, e.whenTrue);
            e = std::move(right);
            break;
        case Binary::Kind::Compare:
            toStack(right, true);
            e = Expr{};
            e.test = code().conditionalJump(op->op, line());
            break;
        case Binary::Kind::Concat:
            toStack(right, true);
            code().emit(LuaOpcode::Concat, 2, line());
            e = Expr{};
            break;
        case Binary::Kind::Arithmetic:
            toStack(right, true);
            code().emit(op->op, 0, line());
            e = Expr{};
            break;
        }
    }
    return true;
}

// A literal, a constructor, a function, or a primary expression.
bool LuaParser::simpleExpression(Expr& e) {
    e = Expr{};
    switch (m_lex.token()) {
    case LuaToken::Number:
        pushNumber(m_lex.number());
        return next();
    case LuaToken::String:
        code().emit(LuaOpcode::PushString, static_cast<std::int32_t>(code().stringConstant(m_lex.text())),
                    m_lex.line());
        return next();
    case LuaToken::Nil:
        code().emit(LuaOpcode::PushNil, 1, m_lex.line());
        return next();
    case LuaToken::LeftBrace:
        return constructor();
    case LuaToken::Function: {
        const int at = m_lex.line();
        return next() && functionBody(false, at, e);
    }
    default:
        return primaryExpression(e);
    }
}

// A name, `%name` or a parenthesised expression, then any number of `.name`, `[key]`, `:method(args)` and `(args)`.
bool LuaParser::primaryExpression(Expr& e) {
    e = Expr{};
    switch (m_lex.token()) {
    case LuaToken::Name: {
        std::string name;
        if (!expectName(name) || !variable(name, e)) {
            return false;
        }
        break;
    }
    case LuaToken::LeftParen: {
        const int at = m_lex.line();
        if (!next() || !expression(e) || !expectMatch(LuaToken::RightParen, LuaToken::LeftParen, at)) {
            return false;
        }
        break;
    }
    case LuaToken::Percent: {
        std::string name;
        if (!next() || !expectName(name) || !upvalue(name, e)) {
            return false;
        }
        break;
    }
    default:
        return failHere("unexpected symbol");
    }
    while (true) {
        switch (m_lex.token()) {
        case LuaToken::Dot: { // t.name
            std::string field;
            if (!next() || !expectName(field)) {
                return false;
            }
            toStack(e, true);
            code().emit(LuaOpcode::PushString, static_cast<std::int32_t>(code().stringConstant(field)), line());
            e = Expr{Expr::Kind::Indexed, static_cast<std::uint32_t>(code().level() - 2), {}, {}, -1};
            break;
        }
        case LuaToken::LeftBracket: { // t[key]
            toStack(e, true);
            Expr key;
            if (!next() || !expression(key)) {
                return false;
            }
            toStack(key, true);
            if (!expect(LuaToken::RightBracket)) {
                return false;
            }
            e = Expr{Expr::Kind::Indexed, static_cast<std::uint32_t>(code().level() - 2), {}, {}, -1};
            break;
        }
        case LuaToken::Colon: { // t:method(args): the method, with t above it as the first argument
            std::string method;
            if (!next() || !expectName(method)) {
                return false;
            }
            toStack(e, true);
            code().emit(LuaOpcode::PushSelf, static_cast<std::int32_t>(code().stringConstant(method)), line());
            if (!callArguments(e, code().level() - 2)) {
                return false;
            }
            break;
        }
        case LuaToken::LeftParen:
        case LuaToken::String:
        case LuaToken::LeftBrace: // f(args), f"text", f{table}
            toStack(e, true);
            if (!callArguments(e, code().level() - 1)) {
                return false;
            }
            break;
        default:
            return true;
        }
    }
}

// The arguments of a call whose function is in slot `base`, then CALL keeping all its results for now.
bool LuaParser::callArguments(Expr& e, int base) {
    switch (m_lex.token()) {
    case LuaToken::LeftParen: {
        const int at = m_lex.line();
        if (!next()) {
            return false;
        }
        if (m_lex.token() != LuaToken::RightParen) {
            Expr last;
            int count = 0;
            if (!expressionList(last, count)) {
                return false;
            }
            if (last.kind != Expr::Kind::Call) {
                toStack(last, true);
            }
        }
        if (!expectMatch(LuaToken::RightParen, LuaToken::LeftParen, at)) {
            return false;
        }
        break;
    }
    case LuaToken::LeftBrace:
        if (!constructor()) {
            return false;
        }
        break;
    default: // a string
        code().emit(LuaOpcode::PushString, static_cast<std::int32_t>(code().stringConstant(m_lex.text())),
                    m_lex.line());
        if (!next()) {
            return false;
        }
        break;
    }
    e = Expr{};
    e.kind = Expr::Kind::Call;
    e.index = static_cast<std::uint32_t>(code().emit(LuaOpcode::Call, base, line(), kLuaMultipleResults));
    return true;
}

// { list ; record } or { record ; list }: CREATETABLE sized for every item, the list items stored by SETLIST every
// 62 and the record pairs by SETMAP every 31. Lua 4.0 keeps the two parts apart.
bool LuaParser::constructor() {
    const int at = m_lex.line();
    if (!next()) {
        return false;
    }
    const int create = code().emit(LuaOpcode::CreateTable, 0, line());
    int items = 0;
    const auto isRecord = [this] {
        return m_lex.token() == LuaToken::LeftBracket ||
               (m_lex.token() == LuaToken::Name && m_lex.lookahead() == LuaToken::Assign);
    };
    const auto endsPart = [this] {
        return m_lex.token() == LuaToken::RightBrace || m_lex.token() == LuaToken::Semicolon;
    };
    std::optional<bool> firstWasRecord;
    if (!endsPart()) {
        firstWasRecord = isRecord();
        if (!(*firstWasRecord ? recordPart(items) : listPart(items))) {
            return false;
        }
    }
    if (m_lex.token() == LuaToken::Semicolon) {
        if (!next()) {
            return false;
        }
        if (m_lex.token() != LuaToken::RightBrace) {
            // The second part is the other kind; a record part where a list was is refused by the record reader.
            const bool record = firstWasRecord ? !*firstWasRecord : isRecord();
            if (!(record ? recordPart(items) : listPart(items))) {
                return false;
            }
        }
    }
    if (!expectMatch(LuaToken::RightBrace, LuaToken::LeftBrace, at)) {
        return false;
    }
    code().replace(create, luaInstructionU(LuaOpcode::CreateTable, static_cast<std::uint32_t>(items)));
    return true;
}

// The list part: values separated by `,`, stored 62 at a time.
bool LuaParser::listPart(int& items) {
    std::uint32_t pending = 0;
    while (true) {
        Expr value;
        if (!expression(value)) {
            return false;
        }
        toStack(value, true);
        ++items;
        if (++pending == kLuaFieldsPerFlush) {
            code().emit(LuaOpcode::SetList, (items - 1) / static_cast<int>(kLuaFieldsPerFlush), line(), pending);
            pending = 0;
        }
        if (m_lex.token() != LuaToken::Comma) {
            break;
        }
        if (!next()) {
            return false;
        }
        if (m_lex.token() == LuaToken::RightBrace || m_lex.token() == LuaToken::Semicolon) {
            break;
        }
    }
    if (pending > 0) {
        code().emit(LuaOpcode::SetList, (items - 1) / static_cast<int>(kLuaFieldsPerFlush), line(), pending);
    }
    return true;
}

// The record part: `name = value` and `[key] = value` separated by `,`, stored 31 pairs at a time.
bool LuaParser::recordPart(int& items) {
    std::int32_t pending = 0;
    while (true) {
        if (m_lex.token() == LuaToken::Name) {
            code().emit(LuaOpcode::PushString, static_cast<std::int32_t>(code().stringConstant(m_lex.text())),
                        m_lex.line());
            if (!next()) {
                return false;
            }
        } else if (m_lex.token() == LuaToken::LeftBracket) {
            Expr key;
            if (!next() || !expression(key)) {
                return false;
            }
            toStack(key, true);
            if (!expect(LuaToken::RightBracket)) {
                return false;
            }
        } else {
            return failHere("<name> or '[' expected");
        }
        Expr value;
        if (!expect(LuaToken::Assign) || !expression(value)) {
            return false;
        }
        toStack(value, true);
        ++items;
        if (++pending == static_cast<std::int32_t>(kLuaPairsPerFlush)) {
            code().emit(LuaOpcode::SetMap, pending, line());
            pending = 0;
        }
        if (m_lex.token() != LuaToken::Comma) {
            break;
        }
        if (!next()) {
            return false;
        }
        if (m_lex.token() == LuaToken::RightBrace || m_lex.token() == LuaToken::Semicolon) {
            break;
        }
    }
    if (pending > 0) {
        code().emit(LuaOpcode::SetMap, pending, line());
    }
    return true;
}

// Expressions separated by `,`: every one but the last on the stack, one value each; the last is left to the caller
// (a call keeps all its results until adjust() or the caller decides).
bool LuaParser::expressionList(Expr& last, int& count) {
    count = 1;
    if (!expression(last)) {
        return false;
    }
    while (m_lex.token() == LuaToken::Comma) {
        toStack(last, true);
        last = Expr{};
        if (!next() || !expression(last)) {
            return false;
        }
        ++count;
    }
    return true;
}

// Makes `count` values, the last of them `last`, into exactly `wanted`: a final call gives as many results as are
// missing, missing values are nils, extra ones are dropped.
void LuaParser::adjust(int wanted, int count, Expr& last) {
    if (last.kind == Expr::Kind::Call) {
        const int results = wanted - (count - 1);
        closeCall(last, std::max(results, 0));
        if (results < 0) {
            code().emit(LuaOpcode::Pop, -results, line());
        }
        return;
    }
    toStack(last, true);
    if (wanted > count) {
        code().emit(LuaOpcode::PushNil, wanted - count, line());
    } else if (count > wanted) {
        code().emit(LuaOpcode::Pop, count - wanted, line());
    }
}

// The variable `name`: a local of this function, or a global. A local of an enclosing function cannot be reached in
// Lua 4.0, only copied into an upvalue with `%name`.
bool LuaParser::variable(const std::string& name, Expr& e) {
    e = Expr{};
    const auto& locals = m_fs->locals;
    if (const auto found = std::find(locals.rbegin(), locals.rend(), name); found != locals.rend()) {
        e.kind = Expr::Kind::Local;
        e.index = static_cast<std::uint32_t>(std::distance(found, locals.rend()) - 1);
        return true;
    }
    for (const Function* outer = m_fs->parent; outer != nullptr; outer = outer->parent) {
        if (std::ranges::find(outer->locals, name) != outer->locals.end()) {
            return failHere(std::format("cannot reach local '{}' of an enclosing function (use %{})", name, name));
        }
    }
    e.kind = Expr::Kind::Global;
    e.index = code().stringConstant(name);
    return true;
}

// `%name`: the value of `name` when the closure is made, a local of the enclosing function or a global.
bool LuaParser::upvalue(const std::string& name, Expr& e) {
    const Function* parent = m_fs->parent;
    if (parent == nullptr) {
        return failHere("cannot use an upvalue in the main chunk");
    }
    const std::string misplaced = std::format("upvalue '{}' must be global or a local of the enclosing function", name);
    if (std::ranges::find(m_fs->locals, name) != m_fs->locals.end()) {
        return failHere(misplaced);
    }
    Upvalue wanted{name, false, 0};
    const auto& outerLocals = parent->locals;
    if (const auto found = std::find(outerLocals.rbegin(), outerLocals.rend(), name); found != outerLocals.rend()) {
        wanted.isLocal = true;
        wanted.slot = static_cast<std::uint32_t>(std::distance(found, outerLocals.rend()) - 1);
    } else {
        for (const Function* outer = parent->parent; outer != nullptr; outer = outer->parent) {
            if (std::ranges::find(outer->locals, name) != outer->locals.end()) {
                return failHere(misplaced);
            }
        }
    }
    auto& upvalues = m_fs->upvalues;
    const auto same = std::ranges::find_if(upvalues, [&](const Upvalue& u) {
        return u.name == wanted.name && u.isLocal == wanted.isLocal && u.slot == wanted.slot;
    });
    std::size_t index = static_cast<std::size_t>(same - upvalues.begin());
    if (same == upvalues.end()) {
        if (upvalues.size() >= kMaxUpvalues) {
            return failHere("too many upvalues");
        }
        upvalues.push_back(std::move(wanted));
    }
    e = Expr{};
    code().emit(LuaOpcode::PushUpvalue, static_cast<std::int32_t>(index), line());
    return true;
}

// A number literal: PUSHINT when it is whole and fits the operand, else a number constant.
void LuaParser::pushNumber(double value) {
    if (std::floor(value) == value && std::abs(value) <= static_cast<double>(kLuaMaxArgS)) {
        code().emit(LuaOpcode::PushInt, static_cast<std::int32_t>(value), m_lex.line());
    } else {
        code().emit(LuaOpcode::PushNum, static_cast<std::int32_t>(code().numberConstant(value)), m_lex.line());
    }
}

// ---- Values and tests ----

// Reads a variable (GETGLOBAL, GETLOCAL, GETTABLE), so its value is on the stack; a call stays as it is.
void LuaParser::discharge(Expr& e) {
    switch (e.kind) {
    case Expr::Kind::Global:
        code().emit(LuaOpcode::GetGlobal, static_cast<std::int32_t>(e.index), line());
        break;
    case Expr::Kind::Local:
        code().emit(LuaOpcode::GetLocal, static_cast<std::int32_t>(e.index), line());
        break;
    case Expr::Kind::Indexed:
        code().emit(LuaOpcode::GetTable, 0, line());
        break;
    default:
        return;
    }
    e.kind = Expr::Kind::Value;
}

// discharge(), and a call keeps exactly one result.
void LuaParser::dischargeOne(Expr& e) {
    discharge(e);
    if (e.kind == Expr::Kind::Call) {
        closeCall(e, 1);
    }
}

// Puts the expression's value on the stack: one value when `onlyOne` (else a call keeps all its results), and a
// condition's jumps made into a value.
void LuaParser::toStack(Expr& e, bool onlyOne) {
    discharge(e);
    if (e.kind == Expr::Kind::Call) {
        if (onlyOne) {
            closeCall(e, 1);
        }
        return;
    }
    if (e.test < 0 && e.whenTrue.empty() && e.whenFalse.empty()) {
        return;
    }
    // A test of a value can keep that value as the result (JMPONT, JMPONF); a comparison has none to keep, so it needs
    // a nil or a 1 pushed for it.
    // Whether the jump at `at` is a test of a value `op` (JMPT or JMPF), not a comparison merged into one.
    const auto keepsValue = [this](int at, LuaOpcode op) {
        return luaOpcodeOf(code().at(at)) == op && !code().isComparison(at);
    };
    const auto allAre = [&](const LuaJumpList& list, LuaOpcode op) {
        return std::ranges::all_of(list, [&](int at) { return keepsValue(at, op); });
    };
    int pushNil = -1;
    int pushOne = -1;
    if (e.test >= 0 || !allAre(e.whenFalse, LuaOpcode::JmpF) || !allAre(e.whenTrue, LuaOpcode::JmpT)) {
        LuaJumpList over;
        if (e.test < 0) {
            // The value is on the stack already: jump over the two pushes, past a POP that only keeps the count.
            over = code().jump(line());
            code().emit(LuaOpcode::Pop, 1, line());
        }
        pushNil = code().emit(LuaOpcode::PushNilJmp, 0, line());
        pushOne = code().emit(LuaOpcode::PushInt, 1, line());
        code().patchToHere(over);
    }
    const int end = code().label();
    const auto settle = [&](const LuaJumpList& list, LuaOpcode test, LuaOpcode keep, int push) {
        for (const int at : list) {
            const bool keeps = keepsValue(at, test);
            const int target = keeps ? end : push;
            const LuaOpcode op = keeps ? keep : luaOpcodeOf(code().at(at));
            code().replace(at, luaInstructionS(op, target - (at + 1)));
        }
    };
    settle(e.whenFalse, LuaOpcode::JmpF, LuaOpcode::JmpOnF, pushNil);
    settle(e.whenTrue, LuaOpcode::JmpT, LuaOpcode::JmpOnT, pushOne);
    // The final test goes to the 1 whatever it is: `x ~= nil` merged into a JMPT has no value to keep either, so it
    // must not become a JMPONT as a test of a value would (luaK_tostack puts it in the true list as a plain jump).
    if (e.test >= 0) {
        const std::uint32_t word = code().at(e.test);
        code().replace(e.test, luaInstructionS(luaOpcodeOf(word), pushOne - (e.test + 1)));
    }
    e = Expr{};
}

// Fixes the open call `e` to keep `results` results.
void LuaParser::closeCall(Expr& e, int results) {
    const auto at = static_cast<int>(e.index);
    const std::uint32_t base = luaArgA(code().at(at));
    code().replace(at, luaInstructionAB(LuaOpcode::Call, base, static_cast<std::uint32_t>(results)));
    code().setLevel(static_cast<int>(base) + results);
    e = Expr{};
}

// Goes on when `e` is true: its false exits are collected, its true exits land here.
void LuaParser::goIfTrue(Expr& e) {
    dischargeOne(e);
    if (e.test >= 0) {
        invertTest(e.test);
        e.whenFalse.push_back(e.test);
    } else if (const int jump = code().conditionalJump(LuaOpcode::JmpF, line()); jump >= 0) {
        e.whenFalse.push_back(jump);
    }
    code().patchToHere(e.whenTrue);
    e.whenTrue.clear();
    e.test = -1;
}

// Goes on when `e` is false: its true exits are collected, its false exits land here.
void LuaParser::goIfFalse(Expr& e) {
    dischargeOne(e);
    if (e.test >= 0) {
        e.whenTrue.push_back(e.test);
    } else if (const int jump = code().conditionalJump(LuaOpcode::JmpT, line()); jump >= 0) {
        e.whenTrue.push_back(jump);
    }
    code().patchToHere(e.whenFalse);
    e.whenFalse.clear();
    e.test = -1;
}

// `not e`: NOT of a value; for a condition, its true and false exits change places and its final test is inverted.
void LuaParser::negate(Expr& e) {
    dischargeOne(e);
    if (e.test < 0 && e.whenTrue.empty() && e.whenFalse.empty()) {
        code().emit(LuaOpcode::Not, 0, line());
        return;
    }
    std::swap(e.whenTrue, e.whenFalse);
    if (e.test >= 0) {
        invertTest(e.test);
    } else {
        code().emit(LuaOpcode::Not, 0, line());
    }
}

// Turns the conditional jump at `at` into the one that jumps when it does not.
void LuaParser::invertTest(int at) {
    const std::uint32_t word = code().at(at);
    code().replace(at, luaInstructionS(invertedTest(luaOpcodeOf(word)), luaArgS(word)));
}

} // namespace coney::script
