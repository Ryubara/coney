// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic Lua 4.0 chunks, assembled instruction by instruction in the tests. Nothing here comes from the game
// (LEGAL.md, "No game data"): the layout and the instruction set are those of the public Lua 4.0.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "support/fixtures.h"

namespace coney::test {

/// Lua 4.0 opcodes the tests assemble, by their number in the instruction set.
enum class LuaOp : std::uint32_t {
    End = 0,
    Return = 1,
    Call = 2,
    PushNil = 4,
    Pop = 5,
    PushInt = 6,
    PushString = 7,
    PushNum = 8,
    PushNegNum = 9,
    PushUpvalue = 10,
    GetLocal = 11,
    GetGlobal = 12,
    GetTable = 13,
    GetDotted = 14,
    CreateTable = 17,
    SetLocal = 18,
    SetGlobal = 19,
    SetTable = 20,
    SetList = 21,
    SetMap = 22,
    Add = 23,
    Mult = 26,
    Concat = 29,
    Not = 31,
    JmpNe = 32,
    JmpLt = 34,
    Jmp = 42,
    ForPrep = 44,
    ForLoop = 45,
    LForPrep = 46,
    LForLoop = 47,
    Closure = 48,
};

/// An instruction with an unsigned operand.
constexpr std::uint32_t luaU(LuaOp op, std::uint32_t u) { return static_cast<std::uint32_t>(op) | (u << 6); }
/// An instruction with a signed operand (jumps, PUSHINT).
constexpr std::uint32_t luaS(LuaOp op, std::int32_t s) {
    constexpr std::int32_t kMaxArgS = ((1 << 26) - 1) >> 1;
    return luaU(op, static_cast<std::uint32_t>(s + kMaxArgS));
}
/// An instruction with A and B operands.
constexpr std::uint32_t luaAB(LuaOp op, std::uint32_t a, std::uint32_t b) {
    return static_cast<std::uint32_t>(op) | (b << 6) | (a << 15);
}

/// One function of a synthetic chunk.
struct LuaFunctionSpec {
    std::uint32_t numParams = 0;
    bool vararg = false;
    std::vector<std::string> strings;
    std::vector<double> numbers;
    std::vector<LuaFunctionSpec> protos;
    std::vector<std::uint32_t> code;
};

/// Appends a Lua string: its size with the terminator, the bytes, the terminator.
inline void luaString(Bytes& out, const std::string& text) {
    out.u32(static_cast<std::uint32_t>(text.size() + 1)).text(text).u8(0);
}

/// Appends one function as Lua 4.0's dumper writes it, with one local and no line info.
inline void luaFunction(Bytes& out, const LuaFunctionSpec& spec) {
    luaString(out, "=(test)");
    out.u32(0).u32(spec.numParams).u8(spec.vararg ? 1 : 0).u32(16);
    out.u32(1);
    luaString(out, "x");
    out.u32(0).u32(1); // the local's live range
    out.u32(0);        // no line info
    out.u32(static_cast<std::uint32_t>(spec.strings.size()));
    for (const std::string& text : spec.strings) {
        luaString(out, text);
    }
    out.u32(static_cast<std::uint32_t>(spec.numbers.size()));
    for (const double number : spec.numbers) {
        const auto bits = std::bit_cast<std::uint64_t>(number);
        out.u32(static_cast<std::uint32_t>(bits)).u32(static_cast<std::uint32_t>(bits >> 32));
    }
    out.u32(static_cast<std::uint32_t>(spec.protos.size()));
    for (const LuaFunctionSpec& nested : spec.protos) {
        luaFunction(out, nested);
    }
    out.u32(static_cast<std::uint32_t>(spec.code.size()));
    for (const std::uint32_t word : spec.code) {
        out.u32(word);
    }
}

/// A complete chunk: the header of the game's layout (little-endian, 4-byte ints, 8-byte numbers), the test number and
/// the main function.
inline std::vector<std::byte> luaChunk(const LuaFunctionSpec& main) {
    Bytes out;
    out.u8(0x1b).text("Lua").u8(0x40).u8(1).u8(4).u8(4).u8(4).u8(32).u8(6).u8(9).u8(8);
    const auto test = std::bit_cast<std::uint64_t>(3.14159265358979323846e8);
    out.u32(static_cast<std::uint32_t>(test)).u32(static_cast<std::uint32_t>(test >> 32));
    luaFunction(out, main);
    return out.data();
}

} // namespace coney::test
