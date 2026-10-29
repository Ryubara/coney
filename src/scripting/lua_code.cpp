// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only part of Coney's own compiler (lua_compiler.h)
#include "scripting/lua_code.h"

#include <algorithm>
#include <bit>

namespace coney::script {

LuaCode::LuaCode(int fixedSlots) {
    m_level = fixedSlots;
    m_proto.maxStackSize = fixedSlots;
}

void LuaCode::setLevel(int level) {
    m_level = level;
    m_proto.maxStackSize = std::max(m_proto.maxStackSize, level);
}

int LuaCode::stackEffect(LuaOpcode op, std::int32_t arg, std::uint32_t b) {
    switch (op) {
    case LuaOpcode::PushNil:
        return arg;
    case LuaOpcode::Pop:
        return -arg;
    case LuaOpcode::PushInt:
    case LuaOpcode::PushString:
    case LuaOpcode::PushNum:
    case LuaOpcode::PushNegNum:
    case LuaOpcode::PushUpvalue:
    case LuaOpcode::GetLocal:
    case LuaOpcode::GetGlobal:
    case LuaOpcode::PushSelf:
    case LuaOpcode::CreateTable:
        return 1;
    case LuaOpcode::GetTable:
    case LuaOpcode::SetLocal:
    case LuaOpcode::SetGlobal:
    case LuaOpcode::Add:
    case LuaOpcode::Sub:
    case LuaOpcode::Mult:
    case LuaOpcode::Div:
    case LuaOpcode::Pow:
    case LuaOpcode::JmpT:
    case LuaOpcode::JmpF:
    case LuaOpcode::JmpOnT:
    case LuaOpcode::JmpOnF:
        return -1;
    case LuaOpcode::SetTable:
    case LuaOpcode::SetList:
        return -static_cast<int>(b);
    case LuaOpcode::SetMap:
        return -2 * arg;
    case LuaOpcode::Concat:
        return 1 - arg;
    case LuaOpcode::JmpNe:
    case LuaOpcode::JmpEq:
    case LuaOpcode::JmpLt:
    case LuaOpcode::JmpLe:
    case LuaOpcode::JmpGt:
    case LuaOpcode::JmpGe:
        return -2;
    case LuaOpcode::ForLoop:
    case LuaOpcode::LForLoop:
        return -3;
    case LuaOpcode::LForPrep:
        return 2;
    case LuaOpcode::Closure:
        return 1 - static_cast<int>(b);
    default: // END, GETDOTTED, GETINDEXED, ADDI, MINUS, NOT, JMP, PUSHNILJMP, FORPREP; the calls set the count
        return 0;
    }
}

namespace {

// Whether `op` takes a signed operand (a jump offset or PUSHINT's number).
bool isSigned(LuaOpcode op) {
    switch (op) {
    case LuaOpcode::PushInt:
    case LuaOpcode::AddI:
    case LuaOpcode::JmpNe:
    case LuaOpcode::JmpEq:
    case LuaOpcode::JmpLt:
    case LuaOpcode::JmpLe:
    case LuaOpcode::JmpGt:
    case LuaOpcode::JmpGe:
    case LuaOpcode::JmpT:
    case LuaOpcode::JmpF:
    case LuaOpcode::JmpOnT:
    case LuaOpcode::JmpOnF:
    case LuaOpcode::Jmp:
    case LuaOpcode::ForPrep:
    case LuaOpcode::ForLoop:
    case LuaOpcode::LForPrep:
    case LuaOpcode::LForLoop:
        return true;
    default:
        return false;
    }
}

// Whether `op` takes the two operands A and B.
bool isAB(LuaOpcode op) {
    return op == LuaOpcode::Call || op == LuaOpcode::TailCall || op == LuaOpcode::SetTable ||
           op == LuaOpcode::SetList || op == LuaOpcode::Closure;
}

// The instruction `op` with its operands.
std::uint32_t encode(LuaOpcode op, std::int32_t arg, std::uint32_t b) {
    if (isAB(op)) {
        return luaInstructionAB(op, static_cast<std::uint32_t>(arg), b);
    }
    if (isSigned(op)) {
        return luaInstructionS(op, arg);
    }
    return luaInstructionU(op, static_cast<std::uint32_t>(arg));
}

} // namespace

void LuaCode::append(std::uint32_t word, int line) {
    // The jumps waiting for this position land on this instruction.
    for (const int from : m_waiting) {
        setTarget(from, pc());
    }
    m_waiting.clear();
    m_proto.code.push_back(word);
    m_proto.lines.push_back(line);
}

void LuaCode::dropLast() {
    m_proto.code.pop_back();
    m_proto.lines.pop_back();
}

// The merges are those of Lua 4.0's code generator (docs/research/formats/lua-bytecode.md#patterns, checked against
// a stock luac's output on our own sources): each turns a pair of instructions into one, or into none, and none
// applies across a jump target.
int LuaCode::emit(LuaOpcode op, std::int32_t arg, int line, std::uint32_t b) {
    // The calls and RETURN set the count instead of changing it.
    if (op == LuaOpcode::Call) {
        setLevel(arg + (b == kLuaMultipleResults ? 1 : static_cast<int>(b)));
    } else if (op == LuaOpcode::Return || op == LuaOpcode::TailCall) {
        setLevel(op == LuaOpcode::Return ? arg : static_cast<int>(b));
    } else {
        addLevel(stackEffect(op, arg, b));
    }
    if (canMerge()) {
        const std::uint32_t previous = m_proto.code.back();
        const LuaOpcode before = luaOpcodeOf(previous);
        const auto replaceLast = [&](std::uint32_t word) { m_proto.code.back() = word; };
        switch (op) {
        case LuaOpcode::PushNil: // nils pushed in a row: one PUSHNIL
            if (before == LuaOpcode::PushNil) {
                replaceLast(luaInstructionU(before, luaArgU(previous) + static_cast<std::uint32_t>(arg)));
                return pc() - 1;
            }
            break;
        case LuaOpcode::Pop: // values dropped after a store: SETTABLE drops them
            if (before == LuaOpcode::SetTable) {
                replaceLast(
                    luaInstructionAB(before, luaArgA(previous), luaArgB(previous) + static_cast<std::uint32_t>(arg)));
                return pc() - 1;
            }
            break;
        case LuaOpcode::GetTable: // t["name"] and t[local]
            if (before == LuaOpcode::PushString || before == LuaOpcode::GetLocal) {
                const LuaOpcode merged = before == LuaOpcode::PushString ? LuaOpcode::GetDotted : LuaOpcode::GetIndexed;
                replaceLast(luaInstructionU(merged, luaArgU(previous)));
                return pc() - 1;
            }
            break;
        case LuaOpcode::Add: // x + k and x - k for a small whole k
        case LuaOpcode::Sub:
            if (before == LuaOpcode::PushInt) {
                const std::int32_t k = luaArgS(previous);
                replaceLast(luaInstructionS(LuaOpcode::AddI, op == LuaOpcode::Add ? k : -k));
                return pc() - 1;
            }
            break;
        case LuaOpcode::Concat: // a .. b .. c: one CONCAT of all three
            if (before == LuaOpcode::Concat) {
                replaceLast(luaInstructionU(before, luaArgU(previous) + static_cast<std::uint32_t>(arg) - 1));
                return pc() - 1;
            }
            break;
        case LuaOpcode::Minus: // a negative literal
            if (before == LuaOpcode::PushInt) {
                replaceLast(luaInstructionS(before, -luaArgS(previous)));
                return pc() - 1;
            }
            if (before == LuaOpcode::PushNum) {
                replaceLast(luaInstructionU(LuaOpcode::PushNegNum, luaArgU(previous)));
                return pc() - 1;
            }
            break;
        case LuaOpcode::JmpNe: // x ~= nil tests x itself
            if (before == LuaOpcode::PushNil && luaArgU(previous) == 1) {
                dropLast();
                append(encode(LuaOpcode::JmpT, arg, 0), line);
                m_comparisons.insert(pc() - 1);
                return pc() - 1;
            }
            break;
        case LuaOpcode::JmpEq: // x == nil is the value `not x`: no jump, and x stays on the stack in place of the nil
            if (before == LuaOpcode::PushNil && luaArgU(previous) == 1) {
                replaceLast(luaInstructionU(LuaOpcode::Not, 0));
                addLevel(1);
                return -1;
            }
            break;
        case LuaOpcode::JmpT: // a test of `not x` tests x the other way; nil is never true
        case LuaOpcode::JmpF: // ...; a whole number is never nil, and nil always is
            if (before == LuaOpcode::Not) {
                dropLast();
                append(encode(op == LuaOpcode::JmpT ? LuaOpcode::JmpF : LuaOpcode::JmpT, arg, 0), line);
                return pc() - 1;
            }
            if (before == LuaOpcode::PushNil && luaArgU(previous) == 1) {
                dropLast();
                if (op == LuaOpcode::JmpT) {
                    return -1;
                }
                append(encode(LuaOpcode::Jmp, arg, 0), line);
                return pc() - 1;
            }
            if (op == LuaOpcode::JmpF && before == LuaOpcode::PushInt) {
                dropLast();
                return -1;
            }
            break;
        case LuaOpcode::Return: // return f(...): the call's results are returned as they are
            if (before == LuaOpcode::Call && luaArgB(previous) == kLuaMultipleResults) {
                replaceLast(luaInstructionAB(LuaOpcode::TailCall, luaArgA(previous), static_cast<std::uint32_t>(arg)));
                return pc() - 1;
            }
            break;
        default:
            break;
        }
    }
    append(encode(op, arg, b), line);
    return pc() - 1;
}

int LuaCode::conditionalJump(LuaOpcode op, int line) {
    // A merge may turn the test into another (JMPT for `x ~= nil`), into a JMP (a test of nil), into a value (NOT for
    // `x == nil`) or into nothing (a test of a whole number, which is never nil); the last two give -1.
    return emit(op, 0, line);
}

LuaJumpList LuaCode::jump(int line) {
    LuaJumpList list = std::move(m_waiting);
    m_waiting.clear();
    list.push_back(pc());
    m_proto.code.push_back(luaInstructionS(LuaOpcode::Jmp, 0));
    m_proto.lines.push_back(line);
    return list;
}

void LuaCode::jumpBack(int target, int line) {
    const LuaJumpList waiting = std::move(m_waiting);
    m_waiting.clear();
    const int from = pc();
    m_proto.code.push_back(luaInstructionS(LuaOpcode::Jmp, target - (from + 1)));
    m_proto.lines.push_back(line);
    patchTo(waiting, target);
}

int LuaCode::label() {
    m_lastTarget = pc();
    return m_lastTarget;
}

void LuaCode::patchToHere(const LuaJumpList& list) {
    label();
    m_waiting.insert(m_waiting.end(), list.begin(), list.end());
}

void LuaCode::patchTo(const LuaJumpList& list, int target) {
    for (const int from : list) {
        setTarget(from, target);
    }
}

void LuaCode::setTarget(int from, int target) {
    const std::uint32_t word = at(from);
    replace(from, luaInstructionS(luaOpcodeOf(word), target - (from + 1)));
}

std::uint32_t LuaCode::stringConstant(const std::string& text) {
    const auto [it, added] = m_strings.try_emplace(text, static_cast<std::uint32_t>(m_proto.strings.size()));
    if (added) {
        m_proto.strings.push_back(text);
    }
    return it->second;
}

std::uint32_t LuaCode::numberConstant(double value) {
    const auto [it, added] =
        m_numbers.try_emplace(std::bit_cast<std::uint64_t>(value), static_cast<std::uint32_t>(m_proto.numbers.size()));
    if (added) {
        m_proto.numbers.push_back(value);
    }
    return it->second;
}

void LuaCode::finish(int line) { append(luaInstruction(LuaOpcode::End), line); }

} // namespace coney::script
