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
enum class LuaOp : std::uint8_t {
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
    Minus = 30,
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

/// Writes one function's code a statement at a time, keeping count of the stack so CALL and SETTABLE get the right
/// operands. Enough for the short synthetic scripts the script-system tests run.
class LuaAsm {
  public:
    /// The index of string constant `text`, added on first use.
    std::uint32_t constant(const std::string& text) {
        for (std::uint32_t i = 0; i < spec.strings.size(); ++i) {
            if (spec.strings[i] == text) {
                return i;
            }
        }
        spec.strings.push_back(text);
        return static_cast<std::uint32_t>(spec.strings.size() - 1);
    }
    /// Pushes the global `name`.
    LuaAsm& getGlobal(const std::string& name) { return emit(luaU(LuaOp::GetGlobal, constant(name)), 1); }
    /// Pops the top into the global `name`.
    LuaAsm& setGlobal(const std::string& name) { return emit(luaU(LuaOp::SetGlobal, constant(name)), -1); }
    /// Pushes a string.
    LuaAsm& pushString(const std::string& text) { return emit(luaU(LuaOp::PushString, constant(text)), 1); }
    /// Pushes a whole number.
    LuaAsm& pushInt(std::int32_t value) { return emit(luaS(LuaOp::PushInt, value), 1); }
    /// Pops the field `name` of the table on the top, pushing its value.
    LuaAsm& getField(const std::string& name) { return emit(luaU(LuaOp::GetDotted, constant(name)), 0); }
    /// Pushes a new table.
    LuaAsm& newTable() { return emit(luaU(LuaOp::CreateTable, 0), 1); }
    /// Pops two numbers and pushes their sum.
    LuaAsm& add() { return emit(luaU(LuaOp::Add, 0), -1); }
    /// Pushes nested function `index` as a closure with no upvalues.
    LuaAsm& closure(std::uint32_t index) { return emit(luaAB(LuaOp::Closure, index, 0), 1); }
    /// With a table, a key and a value pushed, sets table[key] = value and pops all three.
    LuaAsm& setTable() { return emit(luaAB(LuaOp::SetTable, 3, 3), -3); }
    /// Calls the function pushed before the top `args` values, keeping `results` results.
    LuaAsm& call(std::uint32_t args, std::uint32_t results = 0) {
        const auto base = static_cast<std::uint32_t>(m_depth) - args - 1;
        spec.code.push_back(luaAB(LuaOp::Call, base, results));
        m_depth = static_cast<int>(base + results);
        return *this;
    }
    /// Ends the function and returns it.
    LuaFunctionSpec end() {
        spec.code.push_back(luaU(LuaOp::End, 0));
        return spec;
    }

    LuaFunctionSpec spec; ///< The function being written; add nested functions to spec.protos.

  private:
    // Appends `word`, which changes the stack depth by `change`.
    LuaAsm& emit(std::uint32_t word, int change) {
        spec.code.push_back(word);
        m_depth += change;
        return *this;
    }
    int m_depth = 0;
};

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
