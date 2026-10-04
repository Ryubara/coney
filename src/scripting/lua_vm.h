// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "scripting/lua_chunk.h"
#include "scripting/lua_value.h"

namespace coney::script {

/// Limits and leniencies of a LuaVm.
struct LuaVmOptions {
    /// Calling nil does nothing and returns no results instead of failing. The game's configuration scripts call
    /// hundreds of bindings Coney does not have yet; with this set they run through, and nilCalls() counts what was
    /// skipped (docs/research/frontend.md#coneys-implementation: "a binding that is not ready yet can be a no-op").
    bool nilCallsAreNoOps = false;
    /// The deepest call nesting allowed; deeper fails (a runaway recursion in bad data).
    int maxCallDepth = 200;
    /// The most instructions one run() or call() may execute; more fails (an endless loop in bad data). Counting
    /// instructions rather than time keeps a run deterministic.
    std::uint64_t maxInstructions = 50'000'000;
};

/// A Lua 4.0 virtual machine for the game's own precompiled scripts: globals, native bindings and the bytecode
/// interpreter. It is Coney's own implementation of the Lua 4.0 instruction set (the public language's, not the
/// game's code), written so the game's `.lua` files run unchanged.
///
/// Limits, all Coney's: no tag methods (indexing a non-table, arithmetic on a non-number and ordering mixed types
/// fail with ErrorCode::Invalid; numeric strings are not converted for arithmetic); no garbage collector, so tables
/// that refer to each other in a cycle are never freed (the configuration scripts build none); and no standard library
/// unless a caller registers it.
///
/// Research: docs/research/frontend.md#the-front-end-scripts
class LuaVm {
  public:
    explicit LuaVm(LuaVmOptions options = {});

    /// The table of globals.
    [[nodiscard]] Table& globals() { return *m_globals; }
    /// The global `name`; nil when unset.
    [[nodiscard]] Value global(std::string_view name) const { return m_globals->field(name); }
    /// Sets the global `name`.
    void setGlobal(std::string_view name, Value value);
    /// Makes `function` callable from scripts as the global `name`.
    void registerFunction(std::string_view name, NativeFunction function);

    /// Runs the main function of a parsed chunk with no arguments and returns what it returns. Fails with
    /// ErrorCode::Invalid when the bytecode is malformed (an unknown opcode, an operand out of range, the stack
    /// underflowing) or a runtime error occurs (indexing nil, calling a non-function, arithmetic on a non-number), and
    /// as a binding fails. The message names the opcode and its position.
    [[nodiscard]] std::expected<std::vector<Value>, Error> run(std::shared_ptr<const LuaProto> chunk);

    /// Calls `function` with `args` and returns all its results. Fails as run() does, and with ErrorCode::Invalid
    /// when `function` is not a function (or, without nilCallsAreNoOps, is nil).
    [[nodiscard]] std::expected<std::vector<Value>, Error> call(const Value& function, std::span<const Value> args);

    /// Calls of nil skipped under LuaVmOptions::nilCallsAreNoOps.
    [[nodiscard]] std::uint64_t nilCalls() const { return m_nilCalls; }
    /// Instructions executed so far.
    [[nodiscard]] std::uint64_t instructions() const { return m_instructions; }

  private:
    // Calls `function` one level deeper than the current call; the caller adjusts the results.
    std::expected<std::vector<Value>, Error> callNested(const Value& function, std::span<const Value> args);
    // Runs one Lua function's bytecode with its arguments.
    std::expected<std::vector<Value>, Error> execute(const Function& closure, std::span<const Value> args);

    LuaVmOptions m_options;
    std::shared_ptr<Table> m_globals;
    std::uint64_t m_nilCalls = 0;
    std::uint64_t m_instructions = 0;
    std::uint64_t m_budgetStart = 0; // m_instructions when the outermost run() or call() began
    int m_depth = 0;                 // calls in progress, including bindings that run scripts themselves
};

/// Parses a chunk (parseLuaChunk()) into a form LuaVm::run() takes.
[[nodiscard]] std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaChunk(std::span<const std::byte> data);

} // namespace coney::script
