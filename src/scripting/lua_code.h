// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The code emitter of Coney's Lua 4.0 compiler: the instructions of one function, written as Lua 4.0's own code
// generator writes them (docs/research/formats/lua-bytecode.md#patterns). It keeps the count of the stack, merges an
// instruction into the one before it where Lua 4.0 does (`t["k"]` into GETDOTTED, `x - 1` into ADDI, a chain of `..`
// into one CONCAT), and threads jumps: a jump to an unconditional JMP goes to that JMP's target.
// @coney-only part of Coney's own compiler (lua_compiler.h)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "scripting/lua_chunk.h"
#include "scripting/lua_opcodes.h"

namespace coney::script {

/// The positions of jumps that all go to one place once it is known.
using LuaJumpList = std::vector<int>;

/// Writes one function's instructions and constants into a LuaProto.
class LuaCode {
  public:
    /// An emitter for a function whose first `fixedSlots` stack slots hold its parameters (and `arg`).
    explicit LuaCode(int fixedSlots);

    /// Emits `op` with its operand (U or S; A for the A-B instructions, with `b`), at source line `line`, applying
    /// Lua 4.0's merges with the instruction before it unless a jump lands between them. The stack count changes as
    /// the instruction asked for changes it, whatever it was merged into (except `x == nil`, whose NOT keeps x).
    /// Returns the position of the instruction that now does the work, or -1 when the merge left none, or left a
    /// value where a jump was asked for.
    int emit(LuaOpcode op, std::int32_t arg, int line, std::uint32_t b = 0);
    /// Emits a forward JMP and returns it in a list with the jumps waiting for this position (threading).
    [[nodiscard]] LuaJumpList jump(int line);
    /// Emits a JMP to the known position `target`, which the jumps waiting here go to as well.
    void jumpBack(int target, int line);
    /// Emits a conditional jump `op` whose target is not known yet; returns its position, or -1 when the merge with
    /// the instruction before removed it (a test of a constant that always goes on) or made it a value (NOT for
    /// `x == nil`, which the caller then treats as an expression with no jumps).
    [[nodiscard]] int conditionalJump(LuaOpcode op, int line);

    /// Marks the current position as a jump target and returns it: nothing is merged across it.
    int label();
    /// Sends every jump of `list` to the current position, or, if the next instruction is a JMP, to its target.
    void patchToHere(const LuaJumpList& list);
    /// Sends every jump of `list` to `target`.
    void patchTo(const LuaJumpList& list, int target);

    /// The position the next instruction goes to.
    [[nodiscard]] int pc() const { return static_cast<int>(m_proto.code.size()); }
    /// The instruction at `at`.
    [[nodiscard]] std::uint32_t at(int at) const { return m_proto.code[static_cast<std::size_t>(at)]; }
    /// Whether the jump at `at` is a comparison merged into a JMPT (`x ~= nil`): a test with no value to keep, which
    /// a condition used as a value sends to its pushed 1 or nil and never turns into a JMPONT or JMPONF.
    [[nodiscard]] bool isComparison(int at) const { return m_comparisons.contains(at); }
    /// Rewrites the instruction at `at` (an operand fixed up later, an opcode turned into another).
    void replace(int at, std::uint32_t word) { m_proto.code[static_cast<std::size_t>(at)] = word; }

    /// The stack count: values the function holds at this point of its code.
    [[nodiscard]] int level() const { return m_level; }
    /// Sets the stack count, raising the maximum if needed.
    void setLevel(int level);
    /// Changes the stack count by `delta`, raising the maximum if needed.
    void addLevel(int delta) { setLevel(m_level + delta); }
    /// Whether the stack ever passed Lua 4.0's limit of kLuaMaxStack slots.
    [[nodiscard]] bool overflowed() const { return m_proto.maxStackSize > kLuaMaxStack; }

    /// The index of string constant `text`, added on first use.
    [[nodiscard]] std::uint32_t stringConstant(const std::string& text);
    /// The index of number constant `value`, added on first use (equal bits are one constant).
    [[nodiscard]] std::uint32_t numberConstant(double value);

    /// The function being written; complete once finish() has run.
    [[nodiscard]] LuaProto& proto() { return m_proto; }
    /// Emits the END every function ends with.
    void finish(int line);

  private:
    // Appends `word` with its line, sending the jumps waiting here to it.
    void append(std::uint32_t word, int line);
    // Removes the last instruction (merged into the one being emitted).
    void dropLast();
    // Whether the instruction being emitted may be merged with the one before it.
    [[nodiscard]] bool canMerge() const { return pc() > 0 && m_lastTarget != pc(); }
    // Points the jump at `from` to `target`.
    void setTarget(int from, int target);
    // The stack change of `op` with operands `arg` and `b`, as the code generator counts it.
    static int stackEffect(LuaOpcode op, std::int32_t arg, std::uint32_t b);

    LuaProto m_proto;
    int m_level = 0;
    int m_lastTarget = -1;                 // the last position a jump may land on
    LuaJumpList m_waiting;                 // jumps to the current position, waiting to see whether a JMP comes here
    std::unordered_set<int> m_comparisons; // the positions of comparisons merged into a JMPT (isComparison())
    std::unordered_map<std::string, std::uint32_t> m_strings;
    std::unordered_map<std::uint64_t, std::uint32_t> m_numbers; // by the number's bits
};

} // namespace coney::script
