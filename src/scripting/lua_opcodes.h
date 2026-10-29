// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Lua 4.0 instruction set and its encoding, shared by Coney's VM (lua_vm.cpp), its compiler (lua_compiler.cpp)
// and its chunk writer (lua_chunk.cpp). The opcode numbers, operand fields and limits are those of the public Lua 4.0
// that the game links (docs/research/formats/lua-bytecode.md#the-instruction-set).
// @coney-only the instruction set's encoding as data; the original's VM and compiler are stock Lua 4.0.1

#include <cstdint>

namespace coney::script {

/// A Lua 4.0 opcode, in its numbering (the order of Lua 4.0's opcode list).
enum class LuaOpcode : std::uint8_t {
    End,
    Return,
    Call,
    TailCall,
    PushNil,
    Pop,
    PushInt,
    PushString,
    PushNum,
    PushNegNum,
    PushUpvalue,
    GetLocal,
    GetGlobal,
    GetTable,
    GetDotted,
    GetIndexed,
    PushSelf,
    CreateTable,
    SetLocal,
    SetGlobal,
    SetTable,
    SetList,
    SetMap,
    Add,
    AddI,
    Sub,
    Mult,
    Div,
    Pow,
    Concat,
    Minus,
    Not,
    JmpNe,
    JmpEq,
    JmpLt,
    JmpLe,
    JmpGt,
    JmpGe,
    JmpT,
    JmpF,
    JmpOnT,
    JmpOnF,
    Jmp,
    PushNilJmp,
    ForPrep,
    ForLoop,
    LForPrep,
    LForLoop,
    Closure,
};

/// How many opcodes there are.
inline constexpr int kLuaOpcodeCount = static_cast<int>(LuaOpcode::Closure) + 1;

/// The bits of the opcode, in the low end of an instruction.
inline constexpr std::uint32_t kLuaOpBits = 6;
/// The bits of the B operand, above the opcode; A takes the rest.
inline constexpr std::uint32_t kLuaBBits = 9;
/// The largest unsigned operand U (26 bits).
inline constexpr std::uint32_t kLuaMaxArgU = (1U << 26) - 1;
/// A signed operand S is stored as U with this excess, so it runs from -kLuaMaxArgS to kLuaMaxArgS + 1.
inline constexpr std::int32_t kLuaMaxArgS = static_cast<std::int32_t>(kLuaMaxArgU >> 1);
/// The largest A operand (17 bits).
inline constexpr std::uint32_t kLuaMaxArgA = (1U << (32 - kLuaOpBits - kLuaBBits)) - 1;
/// The largest B operand (9 bits).
inline constexpr std::uint32_t kLuaMaxArgB = (1U << kLuaBBits) - 1;
/// The B operand of CALL that keeps every result.
inline constexpr std::uint32_t kLuaMultipleResults = 255;
/// List items one SETLIST stores at most; its A operand counts blocks of this size (stock Lua 4.0.1, and the game's VM
/// multiplies A by 62: docs/research/formats/lua-bytecode.md#limits).
inline constexpr std::uint32_t kLuaFieldsPerFlush = 62;
/// Key-value pairs one SETMAP stores at most: half of kLuaFieldsPerFlush.
inline constexpr std::uint32_t kLuaPairsPerFlush = kLuaFieldsPerFlush / 2;
/// The most stack slots one function may use.
inline constexpr int kLuaMaxStack = 250;

/// The opcode of instruction `word`.
constexpr LuaOpcode luaOpcodeOf(std::uint32_t word) { return static_cast<LuaOpcode>(word & ((1U << kLuaOpBits) - 1)); }
/// The unsigned operand U of `word`.
constexpr std::uint32_t luaArgU(std::uint32_t word) { return word >> kLuaOpBits; }
/// The signed operand S of `word`.
constexpr std::int32_t luaArgS(std::uint32_t word) { return static_cast<std::int32_t>(luaArgU(word)) - kLuaMaxArgS; }
/// The A operand of `word`.
constexpr std::uint32_t luaArgA(std::uint32_t word) { return word >> (kLuaOpBits + kLuaBBits); }
/// The B operand of `word`.
constexpr std::uint32_t luaArgB(std::uint32_t word) { return luaArgU(word) & kLuaMaxArgB; }

/// An instruction with no operand.
constexpr std::uint32_t luaInstruction(LuaOpcode op) { return static_cast<std::uint32_t>(op); }
/// An instruction with the unsigned operand `u`.
constexpr std::uint32_t luaInstructionU(LuaOpcode op, std::uint32_t u) {
    return static_cast<std::uint32_t>(op) | (u << kLuaOpBits);
}
/// An instruction with the signed operand `s`.
constexpr std::uint32_t luaInstructionS(LuaOpcode op, std::int32_t s) {
    return luaInstructionU(op, static_cast<std::uint32_t>(s + kLuaMaxArgS));
}
/// An instruction with the operands `a` and `b`.
constexpr std::uint32_t luaInstructionAB(LuaOpcode op, std::uint32_t a, std::uint32_t b) {
    return static_cast<std::uint32_t>(op) | (b << kLuaOpBits) | (a << (kLuaOpBits + kLuaBBits));
}

} // namespace coney::script
