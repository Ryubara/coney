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
/// 64-bit floating-point numbers (docs/research/frontend.md#the-front-end-scripts). Debug information (local names,
/// line numbers) is read past and dropped.
struct LuaProto {
    std::string source;         ///< The chunk's name as compiled ("=(none)" in the game's files); not used.
    std::int32_t numParams = 0; ///< Fixed parameters.
    bool isVararg = false;      ///< Takes extra arguments in a table `arg`.
    std::int32_t maxStackSize = 0;
    std::vector<std::string> strings; ///< String constants (K operands), raw bytes.
    std::vector<double> numbers;      ///< Number constants (N operands).
    std::vector<LuaProto> protos;     ///< Nested functions (CLOSURE operands).
    std::vector<std::uint32_t> code;  ///< The instructions.
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

} // namespace coney::script
