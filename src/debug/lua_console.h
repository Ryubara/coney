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
/// hook would have run `SCENETEST=1` (docs/research/debug.md#debug-only-script-paths).
///
/// Coney's VM runs compiled Lua 4.0 bytecode and has no compiler, so the console understands the part of Lua's source
/// a console needs, evaluated directly on the VM (globals, tables and calls go through LuaVm, so a binding called here
/// behaves as when a script calls it):
///
/// - statements separated by `;` or new lines: an assignment `name = expr` (also `a.b.c = expr`, `t[k] = expr`) or
///   an expression, whose values are printed (`= 3`); a line starting with `=` prints its expression, as Lua's own
///   console does;
/// - expressions: numbers, strings in `'...'` or `"..."` (with `\n`, `\t`, `\\` and the quote escaped), `nil`, table
///   constructors `{1, 2, x = 3}`, globals, fields (`a.b`, `a[1]`), calls `f(...)` and method calls `obj:m(...)`,
///   unary minus and `not`, `+ - * / ..`, comparisons `== ~= < > <= >=`, `and`, `or`, and parentheses;
/// - not: `if`, loops, `local` and `function` definitions. For those, run a compiled chunk (runFile()).
class LuaConsole {
  public:
    /// A console with an empty history.
    LuaConsole();

    /// Runs `line` in `vm` and returns what it printed: one `= value, ...` line per expression statement that has
    /// values. The line goes into the history (unless it repeats the newest entry). Fails with ErrorCode::Invalid,
    /// naming the column, on a syntax error, and as the VM fails on a runtime error; statements before the failing
    /// one have run.
    [[nodiscard]] std::expected<std::vector<std::string>, Error> run(script::LuaVm& vm, std::string_view line);

    /// Runs a file: a compiled Lua 4.0 chunk (starting with `ESC Lua`) through the VM, or text, line by line, as run()
    /// does. Fails with ErrorCode::NotFound when the file cannot be read, and as the chunk or a line fails.
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

/// Evaluates one Lua expression in `vm` (the console's expression syntax) and returns its values; a call can return
/// several. Fails as LuaConsole::run() does.
[[nodiscard]] std::expected<std::vector<script::Value>, Error> evaluateLua(script::LuaVm& vm, std::string_view text);

} // namespace coney::debug
