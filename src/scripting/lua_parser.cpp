// SPDX-License-Identifier: GPL-3.0-or-later
// Statements, blocks and functions of Coney's Lua 4.0 compiler. Each statement is compiled to the pattern
// docs/research/formats/lua-bytecode.md#patterns gives for it; the two facts that page starts from hold here too: at
// the start of every statement the stack holds exactly the active locals, and jumps are threaded (LuaCode).
// @coney-only part of Coney's own compiler (lua_compiler.h)
#include "scripting/lua_parser.h"

#include <format>
#include <utility>

namespace coney::script {

namespace {

// Lua 4.0's limits on one function (its `llimits.h` values, which the manual's implementation notes give).
constexpr std::size_t kMaxLocals = 200;
constexpr std::size_t kMaxParams = 100;
// How deep statements and subexpressions may nest, so source nested without end gives an error before it uses up the
// C++ stack. Coney's choice (Lua 4.0.1's parser has no such limit); 200 is the limit later Lua's parser puts on its
// own C calls (LUAI_MAXCCALLS), far deeper than any script on the disc.
constexpr int kMaxSyntaxLevels = 200;

// Locals a numeric `for` keeps for itself beside its variable, named so no source can name them.
constexpr std::string_view kForLimit = "(for limit)";
constexpr std::string_view kForStep = "(for step)";
constexpr std::string_view kForTable = "(for table)";

} // namespace

LuaParser::LuaParser(std::string_view source, std::string chunkName, std::string label)
    : m_lex(source, std::move(label)), m_chunkName(std::move(chunkName)) {}

// The whole chunk: its statements until the end of the source, as the main function.
std::expected<LuaProto, Error> LuaParser::compile() {
    if (auto started = m_lex.start(); !started) {
        return std::unexpected(std::move(started.error()));
    }
    Function main(0);
    m_fs = &main;
    if (chunk() && (m_lex.token() == LuaToken::EndOfSource || failHere("'<eof>' expected"))) {
        main.code.finish(line());
        if (main.code.overflowed()) {
            failAt(line(), "function or expression too complex");
        }
    }
    m_fs = nullptr;
    if (m_error) {
        return std::unexpected(std::move(*m_error));
    }
    LuaProto proto = std::move(main.code.proto());
    proto.source = m_chunkName;
    return proto;
}

// ---- Tokens and errors ----

// Moves to the next token; a lexical error stops the compilation.
bool LuaParser::next() {
    if (auto moved = m_lex.advance(); !moved) {
        m_error = std::move(moved.error());
        return false;
    }
    return true;
}

// Skips `token` if it is the current one; false (and no error) otherwise.
bool LuaParser::accept(LuaToken token) {
    if (m_lex.token() != token) {
        return false;
    }
    return next();
}

// Skips `token`, which must be the current one.
bool LuaParser::expect(LuaToken token) {
    if (m_lex.token() != token) {
        return failHere(std::format("'{}' expected", LuaLexer::spelling(token)));
    }
    return next();
}

// Skips `closing`, which ends what `opening` on `line` began; the error says where that was when it is far.
bool LuaParser::expectMatch(LuaToken closing, LuaToken opening, int openedAt) {
    if (m_lex.token() == closing) {
        return next();
    }
    if (openedAt == m_lex.line()) {
        return failHere(std::format("'{}' expected", LuaLexer::spelling(closing)));
    }
    return failHere(std::format("'{}' expected (to close '{}' at line {})", LuaLexer::spelling(closing),
                                LuaLexer::spelling(opening), openedAt));
}

// Reads a name into `name`.
bool LuaParser::expectName(std::string& name) {
    if (m_lex.token() != LuaToken::Name) {
        return failHere("<name> expected");
    }
    name = m_lex.text();
    return next();
}

// Records an error at the current token and returns false, so callers unwind with `return false`.
bool LuaParser::failHere(std::string_view what) {
    if (!m_error) {
        m_error = m_lex.error(what);
    }
    return false;
}

// Records an error at `line` (a limit passed by code already read) and returns false.
bool LuaParser::failAt(int at, std::string_view what) {
    if (!m_error) {
        m_error = m_lex.errorAt(at, what);
    }
    return false;
}

// Whether the current token ends a block.
bool LuaParser::blockFollows() const {
    switch (m_lex.token()) {
    case LuaToken::Else:
    case LuaToken::Elseif:
    case LuaToken::End:
    case LuaToken::Until:
    case LuaToken::EndOfSource:
        return true;
    default:
        return false;
    }
}

// ---- Statements ----

// Statements until a block ends, each optionally followed by `;`; `return` and `break` must be the last.
bool LuaParser::chunk() {
    while (!blockFollows()) {
        bool last = false;
        if (!statement(last)) {
            return false;
        }
        if (m_lex.token() == LuaToken::Semicolon && !next()) {
            return false;
        }
        if (code().overflowed()) {
            return failAt(line(), "function or expression too complex");
        }
        if (last) {
            break;
        }
    }
    return true;
}

// Opens one more statement or subexpression level; an error when there are too many. The caller lowers m_levels again.
bool LuaParser::enterLevel() {
    ++m_levels;
    return m_levels <= kMaxSyntaxLevels || failHere("chunk has too many syntax levels");
}

// A block: its statements, then a POP of the locals it declared.
bool LuaParser::block() {
    const std::size_t outer = m_fs->locals.size();
    if (!chunk()) {
        return false;
    }
    if (const std::size_t inner = m_fs->locals.size() - outer; inner > 0) {
        code().emit(LuaOpcode::Pop, static_cast<std::int32_t>(inner), line());
    }
    dropLocals(outer);
    return true;
}

// One statement, counted as a syntax level; `last` is set for `return` and `break`.
bool LuaParser::statement(bool& last) {
    const bool done = enterLevel() && statementAt(last);
    --m_levels;
    return done;
}

// One statement, once its level is counted.
bool LuaParser::statementAt(bool& last) {
    const int at = m_lex.line();
    switch (m_lex.token()) {
    case LuaToken::If:
        return ifStatement();
    case LuaToken::While:
        return whileStatement();
    case LuaToken::Do:
        return next() && block() && expectMatch(LuaToken::End, LuaToken::Do, at);
    case LuaToken::For:
        return forStatement();
    case LuaToken::Repeat:
        return repeatStatement();
    case LuaToken::Function:
        return functionStatement();
    case LuaToken::Local:
        return localStatement();
    case LuaToken::Return:
        last = true;
        return returnStatement();
    case LuaToken::Break:
        last = true;
        return breakStatement();
    default:
        return expressionStatement();
    }
}

// if c then b {elseif c then b} [else b] end: each condition's false exits jump to the next clause, and a JMP after
// every clause but the last jumps to the end.
bool LuaParser::ifStatement() {
    const int at = m_lex.line();
    LuaJumpList escapes;
    Expr condition;
    if (!next() || !expression(condition)) {
        return false;
    }
    goIfTrue(condition);
    if (!expect(LuaToken::Then) || !block()) {
        return false;
    }
    while (m_lex.token() == LuaToken::Elseif) {
        const LuaJumpList escape = code().jump(line());
        escapes.insert(escapes.end(), escape.begin(), escape.end());
        code().patchToHere(condition.whenFalse);
        condition = Expr{};
        if (!next() || !expression(condition)) {
            return false;
        }
        goIfTrue(condition);
        if (!expect(LuaToken::Then) || !block()) {
            return false;
        }
    }
    if (m_lex.token() == LuaToken::Else) {
        const LuaJumpList escape = code().jump(line());
        escapes.insert(escapes.end(), escape.begin(), escape.end());
        code().patchToHere(condition.whenFalse);
        if (!next() || !block()) {
            return false;
        }
    } else {
        code().patchToHere(condition.whenFalse);
    }
    if (!expectMatch(LuaToken::End, LuaToken::If, at)) {
        return false;
    }
    code().patchToHere(escapes);
    return true;
}

// while c do b end: the condition at the head, the body, a JMP back to the head.
bool LuaParser::whileStatement() {
    const int at = m_lex.line();
    if (!next()) {
        return false;
    }
    const int head = code().label();
    Expr condition;
    if (!expression(condition)) {
        return false;
    }
    goIfTrue(condition);
    Loop loop{code().level(), {}};
    m_fs->loops.push_back(&loop);
    const bool body = expect(LuaToken::Do) && block();
    m_fs->loops.pop_back();
    if (!body) {
        return false;
    }
    code().jumpBack(head, line());
    if (!expectMatch(LuaToken::End, LuaToken::While, at)) {
        return false;
    }
    code().patchToHere(condition.whenFalse);
    code().patchToHere(loop.breaks);
    return true;
}

// repeat b until c: the body, then the condition, whose false exits jump back to the head. The body's locals end
// before the condition.
bool LuaParser::repeatStatement() {
    const int at = m_lex.line();
    if (!next()) {
        return false;
    }
    const int head = code().label();
    Loop loop{code().level(), {}};
    m_fs->loops.push_back(&loop);
    const bool body = block();
    m_fs->loops.pop_back();
    Expr condition;
    if (!body || !expectMatch(LuaToken::Until, LuaToken::Repeat, at) || !expression(condition)) {
        return false;
    }
    goIfTrue(condition);
    code().patchTo(condition.whenFalse, head);
    code().patchToHere(loop.breaks);
    return true;
}

// for name = a, b [, c] do ... end, or for k, v in t do ... end.
bool LuaParser::forStatement() {
    const int at = m_lex.line();
    std::string name;
    if (!next() || !expectName(name)) {
        return false;
    }
    if (m_lex.token() == LuaToken::Assign) {
        return numericFor(name, at);
    }
    if (m_lex.token() == LuaToken::Comma) {
        return tableFor(name, at);
    }
    return failHere("'=' or ',' expected");
}

// for name = a, b [, c]: the three values (a step of 1 when none is given), FORPREP, the body, FORLOOP.
bool LuaParser::numericFor(const std::string& name, int startLine) {
    Loop loop{code().level(), {}};
    Expr value;
    if (!next() || !expression(value)) {
        return false;
    }
    toStack(value, true);
    if (!expect(LuaToken::Comma) || !expression(value)) {
        return false;
    }
    toStack(value, true);
    if (m_lex.token() == LuaToken::Comma) {
        if (!next() || !expression(value)) {
            return false;
        }
        toStack(value, true);
    } else {
        code().emit(LuaOpcode::PushInt, 1, line());
    }
    if (!expect(LuaToken::Do)) {
        return false;
    }
    const int prepAt = code().emit(LuaOpcode::ForPrep, 0, line());
    if (!addLocal(name) || !addLocal(std::string(kForLimit)) || !addLocal(std::string(kForStep))) {
        return false;
    }
    return loopBody(loop, LuaOpcode::ForLoop, prepAt, startLine);
}

// for k, v in t: the table, LFORPREP, the body, LFORLOOP; the table stays below k and v.
bool LuaParser::tableFor(const std::string& name, int startLine) {
    Loop loop{code().level(), {}};
    std::string valueName;
    Expr table;
    if (!next() || !expectName(valueName) || !expect(LuaToken::In) || !expression(table)) {
        return false;
    }
    toStack(table, true);
    if (!expect(LuaToken::Do)) {
        return false;
    }
    const int prepAt = code().emit(LuaOpcode::LForPrep, 0, line());
    if (!addLocal(std::string(kForTable)) || !addLocal(name) || !addLocal(valueName)) {
        return false;
    }
    return loopBody(loop, LuaOpcode::LForLoop, prepAt, startLine);
}

// The body of either `for`, after its preparing instruction at `prepAt`: the block, the stepping instruction back to
// the body's start, then the preparing jump and the breaks sent past the loop. The loop's three locals end with it
// (the VM drops them as the loop ends).
bool LuaParser::loopBody(Loop& loop, LuaOpcode step, int prepAt, int startLine) {
    const std::size_t outer = m_fs->locals.size() - 3;
    const int bodyStart = code().label();
    m_fs->loops.push_back(&loop);
    const bool body = block();
    m_fs->loops.pop_back();
    if (!body) {
        return false;
    }
    const int stepAt = code().pc();
    code().emit(step, bodyStart - (stepAt + 1), line());
    if (!expectMatch(LuaToken::End, LuaToken::For, startLine)) {
        return false;
    }
    code().patchToHere(LuaJumpList{prepAt});
    code().patchToHere(loop.breaks);
    dropLocals(outer);
    return true;
}

// function name[.field | :method] body end: the closure stored as an assignment to that name would store it.
bool LuaParser::functionStatement() {
    const int at = m_lex.line();
    std::string name;
    if (!next() || !expectName(name)) {
        return false;
    }
    Expr target;
    if (!variable(name, target)) {
        return false;
    }
    bool method = false;
    if (m_lex.token() == LuaToken::Dot || m_lex.token() == LuaToken::Colon) {
        method = m_lex.token() == LuaToken::Colon;
        std::string field;
        if (!next() || !expectName(field)) {
            return false;
        }
        toStack(target, true);
        code().emit(LuaOpcode::PushString, static_cast<std::int32_t>(code().stringConstant(field)), line());
        target = Expr{Expr::Kind::Indexed, static_cast<std::uint32_t>(code().level() - 2), {}, {}, -1};
    }
    Expr closure;
    if (!functionBody(method, at, closure)) {
        return false;
    }
    store(target);
    if (target.kind == Expr::Kind::Indexed) {
        code().emit(LuaOpcode::Pop, 2, line());
    }
    return true;
}

// local a, b [= values]: the values stay where they were pushed and become the locals (nils for the missing ones).
bool LuaParser::localStatement() {
    std::vector<std::string> names;
    do {
        std::string name;
        if (!next() || !expectName(name)) {
            return false;
        }
        names.push_back(std::move(name));
    } while (m_lex.token() == LuaToken::Comma);
    const auto wanted = static_cast<int>(names.size());
    if (m_lex.token() == LuaToken::Assign) {
        Expr last;
        int count = 0;
        if (!next() || !expressionList(last, count)) {
            return false;
        }
        adjust(wanted, count, last);
    } else {
        code().emit(LuaOpcode::PushNil, wanted, line());
    }
    for (const std::string& name : names) {
        if (!addLocal(name)) {
            return false;
        }
    }
    return true;
}

// return [values]: RETURN of what is above the locals (TAILCALL when the last value is a call: LuaCode merges it).
bool LuaParser::returnStatement() {
    if (!next()) {
        return false;
    }
    if (!blockFollows() && m_lex.token() != LuaToken::Semicolon) {
        Expr last;
        int count = 0;
        if (!expressionList(last, count)) {
            return false;
        }
        if (last.kind != Expr::Kind::Call) {
            toStack(last, true);
        }
    }
    code().emit(LuaOpcode::Return, static_cast<std::int32_t>(m_fs->locals.size()), line());
    return true;
}

// break: drops the locals of the loop, jumps past it, then restores the count with a PUSHNIL that never runs, so the
// rest of the block is counted as if the break were not there.
bool LuaParser::breakStatement() {
    if (m_fs->loops.empty()) {
        return failHere("no loop to break");
    }
    if (!next()) {
        return false;
    }
    Loop& loop = *m_fs->loops.back();
    const int above = code().level() - loop.entryLevel;
    if (above > 0) {
        code().emit(LuaOpcode::Pop, above, line());
    }
    const LuaJumpList out = code().jump(line());
    loop.breaks.insert(loop.breaks.end(), out.begin(), out.end());
    if (above > 0) {
        code().emit(LuaOpcode::PushNil, above, line());
    }
    return true;
}

// A call, whose results are dropped, or an assignment.
bool LuaParser::expressionStatement() {
    Expr first;
    if (!primaryExpression(first)) {
        return false;
    }
    if (first.kind == Expr::Kind::Call) {
        closeCall(first, 0);
        return true;
    }
    return assignment(first);
}

// targets = values: every target's table and key were pushed as it was read; the values follow, adjusted to the
// number of targets; the stores go from the last target to the first; the tables and keys are dropped at the end.
bool LuaParser::assignment(Expr& first) {
    std::vector<Expr> targets{first};
    while (m_lex.token() == LuaToken::Comma) {
        Expr target;
        if (!next() || !primaryExpression(target)) {
            return false;
        }
        targets.push_back(std::move(target));
    }
    for (const Expr& target : targets) {
        if (target.kind != Expr::Kind::Global && target.kind != Expr::Kind::Local &&
            target.kind != Expr::Kind::Indexed) {
            return failHere("syntax error");
        }
    }
    Expr last;
    int count = 0;
    if (!expect(LuaToken::Assign) || !expressionList(last, count)) {
        return false;
    }
    adjust(static_cast<int>(targets.size()), count, last);
    int indexed = 0;
    for (auto target = targets.rbegin(); target != targets.rend(); ++target) {
        store(*target);
        indexed += target->kind == Expr::Kind::Indexed ? 1 : 0;
    }
    if (indexed > 0) {
        code().emit(LuaOpcode::Pop, 2 * indexed, line());
    }
    return true;
}

// Stores the value on the top into `target`. An indexed target's table and key stay on the stack.
void LuaParser::store(const Expr& target) {
    switch (target.kind) {
    case Expr::Kind::Global:
        code().emit(LuaOpcode::SetGlobal, static_cast<std::int32_t>(target.index), line());
        break;
    case Expr::Kind::Local:
        code().emit(LuaOpcode::SetLocal, static_cast<std::int32_t>(target.index), line());
        break;
    default: // Indexed: SETTABLE counts down from the top to the table.
        code().emit(LuaOpcode::SetTable, code().level() - static_cast<int>(target.index), line(), 1);
        break;
    }
}

// `(params) body end` of a function: a function of its own, then its upvalues pushed and CLOSURE in this one.
bool LuaParser::functionBody(bool method, int startLine, Expr& closure) {
    std::vector<std::string> params;
    if (method) {
        params.emplace_back("self");
    }
    bool vararg = false;
    if (!expect(LuaToken::LeftParen)) {
        return false;
    }
    if (m_lex.token() != LuaToken::RightParen) {
        while (true) {
            if (m_lex.token() == LuaToken::Dots) {
                vararg = true;
                if (!next()) {
                    return false;
                }
                break;
            }
            std::string name;
            if (!expectName(name)) {
                return false;
            }
            params.push_back(std::move(name));
            if (m_lex.token() != LuaToken::Comma) {
                break;
            }
            if (!next()) {
                return false;
            }
        }
    }
    if (!expect(LuaToken::RightParen)) {
        return false;
    }
    if (params.size() > kMaxParams) {
        return failAt(startLine, "too many parameters");
    }

    // The parameters, then `arg` for the extra arguments, take the first slots.
    // No deeper than parseLuaChunk() reads back, so every chunk the compiler writes loads.
    int depth = 1;
    for (const Function* outer = m_fs->parent; outer != nullptr; outer = outer->parent) {
        ++depth;
    }
    if (depth > kMaxLuaNesting) {
        return failAt(startLine, "functions nested too deeply");
    }
    const auto fixed = static_cast<int>(params.size()) + (vararg ? 1 : 0);
    Function inner(fixed);
    inner.parent = m_fs;
    inner.locals = std::move(params);
    if (vararg) {
        inner.locals.emplace_back("arg");
    }
    m_fs = &inner;
    const bool body = chunk() && expectMatch(LuaToken::End, LuaToken::Function, startLine);
    if (body) {
        inner.code.finish(line());
    }
    m_fs = inner.parent;
    if (!body) {
        return false;
    }
    if (inner.code.overflowed()) {
        return failAt(startLine, "function or expression too complex");
    }
    LuaProto proto = std::move(inner.code.proto());
    proto.source = m_chunkName;
    proto.lineDefined = startLine;
    proto.numParams = fixed - (vararg ? 1 : 0);
    proto.isVararg = vararg;

    // The closure: each upvalue's value as the enclosing function sees it now, then CLOSURE.
    for (const Upvalue& value : inner.upvalues) {
        if (value.isLocal) {
            code().emit(LuaOpcode::GetLocal, static_cast<std::int32_t>(value.slot), line());
        } else {
            code().emit(LuaOpcode::GetGlobal, static_cast<std::int32_t>(code().stringConstant(value.name)), line());
        }
    }
    auto& protos = code().proto().protos;
    protos.push_back(std::move(proto));
    code().emit(LuaOpcode::Closure, static_cast<std::int32_t>(protos.size() - 1), line(),
                static_cast<std::uint32_t>(inner.upvalues.size()));
    closure = Expr{};
    return true;
}

// Makes `name` the next local of the function, in the next stack slot.
bool LuaParser::addLocal(const std::string& name) {
    if (m_fs->locals.size() >= kMaxLocals) {
        return failHere("too many local variables");
    }
    m_fs->locals.push_back(name);
    return true;
}

// Ends the locals past the first `keep` (their scope is over; any POP was emitted by the caller).
void LuaParser::dropLocals(std::size_t keep) { m_fs->locals.resize(keep); }

} // namespace coney::script
