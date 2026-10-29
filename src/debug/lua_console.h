// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"

namespace coney::debug {

/// The debug menus' Lua console: runs a typed line in the game's script state, as the original's dormant scene-test
/// hook would have run `SCENETEST = "l99_c1"` (docs/research/debug.md#debug-only-script-paths).
///
/// A line is Lua 4.0, compiled by Coney's compiler (scripting/lua_compiler.h) and run on the VM, so globals, tables
/// and the bindings it calls behave as when a script does: statements, `local`, `if`, loops and `function`
/// definitions. An expression, or a line starting with `=`, prints its values (`= 3`), as Lua's own console does;
/// so does a line ending in `return`. Errors name the line as `console:<line>:`.
class LuaConsole {
  public:
    /// A console with an empty history.
    LuaConsole();

    /// Runs `line` in `vm` and returns what it printed: one `= value, ...` line when it has values, nothing
    /// otherwise. The line goes into the history (unless it repeats the newest entry). Fails with
    /// ErrorCode::Invalid, naming the line, on a syntax error (nothing runs), and as the VM fails on a runtime error
    /// (the statements before the failing one have run).
    [[nodiscard]] std::expected<std::vector<std::string>, Error> run(script::LuaVm& vm, std::string_view line);

    /// Runs a file: a compiled Lua 4.0 chunk (starting with `ESC Lua`) or Lua source, as one chunk, and prints what it
    /// returns (or `<path>: ran` when it returns nothing). Fails with ErrorCode::NotFound when the file cannot be
    /// read, and as the chunk fails to load or run.
    [[nodiscard]] std::expected<std::vector<std::string>, Error> runFile(script::LuaVm& vm, const std::string& path);

    /// The lines run, oldest first; shared with the menu item that recalls them.
    [[nodiscard]] const std::shared_ptr<std::vector<std::string>>& history() const { return m_history; }

    /// The longest history kept.
    static constexpr std::size_t kHistoryLength = 50;

  private:
    // Adds `line` to the history.
    void remember(std::string_view line);

    std::shared_ptr<std::vector<std::string>> m_history;
};

/// Evaluates one Lua 4.0 expression in `vm` and returns its values; a call can return several. Fails as
/// LuaConsole::run() does.
[[nodiscard]] std::expected<std::vector<script::Value>, Error> evaluateLua(script::LuaVm& vm, std::string_view text);

} // namespace coney::debug
