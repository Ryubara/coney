// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"

namespace coney::script {

/// The text Lua 4.0's `tostring` gives a value: numbers as `%.16g`, strings unchanged, `nil`, and `table: ...` or
/// `function: ...` with an address for the others.
[[nodiscard]] std::string luaToString(const Value& value);

/// What the base library asks of the script system that owns the state.
struct BaseLibraryHooks {
    /// `dofile(name)`: runs the script `name` (a WAD name, used as given) and returns its results; empty runs nothing.
    std::function<std::expected<std::vector<Value>, Error>(std::string_view name)> doFile;
    /// `print(...)`: one line, the arguments' `tostring` joined by tabs; empty prints nothing.
    std::function<void(std::string_view line)> print;
};

/// Registers Lua 4.0's base library in `vm`: the 33 functions (`assert`, `call`, `foreach`, `getn`, `sort`,
/// `tinsert`, `tostring`, `type`, ...), the 4.0 compatibility names `foreachvar`, `nextvar`, `rawgetglobal` and
/// `rawsetglobal`, and `_VERSION` = "Lua 4.0.1". This is the public Lua 4.0 library written anew; the game opens it
/// unchanged (docs/research/scripting.md#libraries).
///
/// Coney's limits: there are no tag methods, so `newtag`, `settag`, `settagmethod`, `gettagmethod` and
/// `copytagmethods` keep nothing (`tag` returns a value's type tag); `dostring` runs only precompiled chunks (Coney has
/// no Lua compiler, and the game's scripts are all precompiled); `collectgarbage` and `gcinfo` do nothing (Coney's VM
/// counts references). `_ERRORMESSAGE` and `_ALERT` are left to the owner: the game's script system replaces them.
void openBaseLibrary(LuaVm& vm, BaseLibraryHooks hooks);

/// Registers Lua 4.0's string library in `vm`: `strlen`, `strsub`, `strlower`, `strupper`, `strchar`, `strrep`,
/// `strbyte` (also as `ascii`), `format` (also as `strformat`, the game's extra name), `strfind` and `gsub`, with Lua's
/// patterns. Written anew from the public Lua 4.0 library (docs/research/scripting.md#libraries).
void openStringLibrary(LuaVm& vm);

/// Registers Lua 4.0's math library in `vm`: 23 functions and `PI`. As in stock Lua 4.0, `sin`, `cos`, `tan` and
/// their inverses work in degrees. `random` and `randomseed` use Coney's own deterministic generator (seeded with 0),
/// never the C library's, so the engine's test mode stays reproducible; the game replaces `random` with a binding
/// anyway. The `^` operator needs no tag method: Coney's VM raises to a power itself.
/// Research: docs/research/scripting.md#libraries
void openMathLibrary(LuaVm& vm);

} // namespace coney::script
