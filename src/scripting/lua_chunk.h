// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "core/error.h"

namespace coney::script {

/// One function of a precompiled Lua 4.0 chunk: its constants, the functions nested in it and its bytecode. The main
/// function of a chunk is the file's top level.
///
/// The game's `.lua` files are Lua 4.0 bytecode for a little-endian machine with 32-bit integers and instructions and
/// 64-bit floating-point numbers (docs/research/frontend.md#the-front-end-scripts). Their debug information (local
/// names, line numbers) is read past and dropped; the game's chunks carry none anyway. Coney's own compiler
/// (lua_compiler.h) fills `lines`, so an error in a compiled source names its line.
struct LuaProto {
    std::string source;           ///< The chunk's name as compiled ("=(none)" in the game's files).
    std::int32_t lineDefined = 0; ///< The line of the `function` keyword; 0 for a chunk's main function.
    std::int32_t numParams = 0;   ///< Fixed parameters.
    bool isVararg = false;        ///< Takes extra arguments in a table `arg`.
    std::int32_t maxStackSize = 0;
    std::vector<std::string> strings; ///< String constants (K operands), raw bytes.
    std::vector<double> numbers;      ///< Number constants (N operands).
    std::vector<LuaProto> protos;     ///< Nested functions (CLOSURE operands).
    std::vector<std::uint32_t> code;  ///< The instructions.
    /// The source line of each instruction, one per entry of `code`; empty when the chunk has none (every chunk read
    /// by parseLuaChunk()).
    std::vector<std::int32_t> lines;
};

/// How deep functions may nest in a chunk; deeper means damaged data, not a script.
inline constexpr int kMaxLuaNesting = 64;

/// Parses a precompiled Lua 4.0 chunk ("\x1bLua", version 0x40). Accepts only the layout the game uses: little-endian,
/// 4-byte int, size_t and instruction, 6-bit opcodes, 9-bit B operands and 8-byte numbers, checked through the
/// header's test number as Lua's own loader does.
///
/// Fails with ErrorCode::Invalid for another signature, version or layout, a nesting deeper than kMaxLuaNesting or a
/// negative count, and ErrorCode::Truncated when the data ends early. Bytes after the main function are refused too
/// (ErrorCode::Invalid): a chunk is one function.
[[nodiscard]] std::expected<LuaProto, Error> parseLuaChunk(std::span<const std::byte> data);

/// Writes `main` as a precompiled Lua 4.0 chunk in the game's layout, stripped as `luac -s` strips one: every
/// function's source is "=(none)", with no local-variable records and no line table, and only `lineDefined` kept
/// (docs/research/formats/lua-bytecode.md#what-a-chunk-keeps-and-what-stripping-removes). parseLuaChunk() reads it
/// back to the same functions. Coney's compiler output goes through it for the round trip with `luac`'s chunks
/// (`coney_luac`, docs/guides/coney-tools-fidelity.md#lua); the game never runs a written chunk.
[[nodiscard]] std::vector<std::byte> writeLuaChunk(const LuaProto& main);

} // namespace coney::script
