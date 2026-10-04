// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/language.h"
#include "fileio/wad.h"
#include "gui/global_strings.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"

namespace coney::script {

/// What `GetPlatform()` returns to the scripts: always 1 on the PS2 (`0x00357998`,
/// docs/research/scripting.md#bindings-whose-results-the-front-end-needs). The language files choose between two
/// wordings of about twenty strings by `Platform == 2`; 1 takes the PS2's ("PRESS THE START BUTTON", triangle for
/// back).
inline constexpr double kPlatformValue = 1.0;

/// Registers the string bindings in `vm`, each storing its `(id, text)` arguments in `strings`: `CfgHUDMessage` into
/// StringTable::Hud, and `CfgCrimeMessage`, `CfgTutorialMessage`, `CfgWarriorCommand` and `CfgAnnounceMessage` into
/// the other tables. A call whose id is not a whole number from 0 to 2^32 - 1, or whose text is not a string, fails
/// with ErrorCode::Invalid. `strings` must outlive `vm`.
/// @orig 0x0035e5d0 CfgHUDMessage (unknown)
void addStringBindings(LuaVm& vm, gui::GlobalStrings& strings);

/// Counts from a string load, for logs and the disc check.
struct StringLoadReport {
    std::uint64_t instructions = 0; ///< Lua instructions executed.
    std::uint64_t skippedCalls = 0; ///< Calls of bindings Coney does not have yet, skipped as no-ops.
};

/// Fills `strings` for `language` the way the game does: runs `enum_preload.lua` (the enumerations the configuration
/// uses), then `config_preload2.lua`, which runs `config_strings_<code>.lua` for the language and passes every entry of
/// its tables to the string bindings (addStringBindings()). The scripts are the game's own bytecode, run by LuaVm with
/// `GetLanguage`, `GetPlatform` (kPlatformValue) and `doFile` provided and every other binding skipped
/// (LuaVmOptions::nilCallsAreNoOps).
///
/// Fails as `source` does for a missing script, as parseLuaChunk() does for a damaged one, and as LuaVm::run() does
/// when a script fails.
///
/// Research: docs/research/gui.md#strings, docs/research/frontend.md#the-front-end-scripts
[[nodiscard]] std::expected<StringLoadReport, Error> loadGlobalStrings(const ScriptSource& source, Language language,
                                                                       gui::GlobalStrings& strings);

/// A ScriptSource over the WAD: the entry `./ee_files/<name>`. Fails with ErrorCode::NotFound for no such entry and as
/// the read does otherwise. `wad` must outlive the result.
[[nodiscard]] ScriptSource wadScriptSource(const io::Wad& wad);

} // namespace coney::script
