// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Coney's Lua 4.0 compiler: Lua 4.0 source text to the LuaProto that Coney's VM runs. The lexer and the parser are
// written from the Lua 4.0 reference manual's grammar; the code follows the patterns of Lua 4.0's own code generator
// as docs/research/formats/lua-bytecode.md#patterns describes them, so a decompiled game script compiles back to the
// game's own instructions wherever those patterns reach.
// @coney-only Coney's own compiler; the original links stock Lua 4.0.1's parser (0x0032d828), which is not ported

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "core/error.h"
#include "scripting/lua_chunk.h"

namespace coney::script {

/// Compiles the Lua 4.0 chunk `source` into its main function, with line information on every instruction.
///
/// `chunkName` names the chunk in errors, as Lua's chunk names do: `=name` is shown as `name`, `@file` as `file`, and
/// anything else as `[string "..."]` (luaChunkLabel()); it is stored as the functions' `source`.
///
/// Fails with ErrorCode::Invalid on a lexical or syntax error, or a limit of Lua 4.0 passed (a function needing more
/// than 250 stack slots, 200 locals, 32 upvalues or 100 parameters), with the message
/// `<chunk>:<line>: <what> near '<token>'`.
[[nodiscard]] std::expected<LuaProto, Error> compileLua(std::string_view source, std::string_view chunkName);

/// Compiles `source` (compileLua()) into the form LuaVm::run() takes.
[[nodiscard]] std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaSource(std::string_view source,
                                                                                  std::string_view chunkName);

/// Loads `buffer` the way Lua 4.0's `dostring` and `dofile` do: a precompiled chunk when its first byte is ESC (the
/// chunk signature's), otherwise source text compiled under `chunkName`.
[[nodiscard]] std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaBuffer(std::span<const std::byte> buffer,
                                                                                  std::string_view chunkName);

/// How errors show the chunk named `chunkName`: `=name` as `name`, `@file` as `file`, other text as
/// `[string "<its first line>"]`, cut to a readable length.
[[nodiscard]] std::string luaChunkLabel(std::string_view chunkName);

} // namespace coney::script
