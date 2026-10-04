// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lua_chunk.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "fileio/reader.h"

namespace coney::script {

namespace {

// The header Lua 4.0 writes before the main function: ESC "Lua", the version, then the layout of the machine that
// compiled the chunk. These are the game's values (little-endian; int, size_t and instruction of 4 bytes; 32-bit
// instructions with 6-bit opcodes and 9-bit B operands; 8-byte numbers).
constexpr std::array<std::uint8_t, 12> kHeader{0x1b, 'L', 'u', 'a', 0x40, 1, 4, 4, 4, 32, 6, 9};
constexpr std::uint8_t kNumberSize = 8;
// The number Lua writes after the header so a loader can check the floating-point format: 3.14159265358979323846e8.
constexpr double kTestNumber = 3.14159265358979323846e8;

// Reads the bytes of a Lua chunk with Lua's own conventions on top of coney::io::Reader.
class ChunkReader {
  public:
    explicit ChunkReader(std::span<const std::byte> data) : m_reader(data) {}

    // A 32-bit int that must not be negative: every int a chunk stores is a count or a size.
    std::expected<std::int32_t, Error> count() {
        auto value = m_reader.readU32Le();
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        if (*value > 0x7fffffffU) {
            return fail(ErrorCode::Invalid, "Lua chunk: a negative count");
        }
        return static_cast<std::int32_t>(*value);
    }

    // An 8-byte little-endian double.
    std::expected<double, Error> number() {
        auto low = m_reader.readU32Le();
        if (!low) {
            return std::unexpected(std::move(low.error()));
        }
        auto high = m_reader.readU32Le();
        if (!high) {
            return std::unexpected(std::move(high.error()));
        }
        return std::bit_cast<double>((static_cast<std::uint64_t>(*high) << 32) | *low);
    }

    // A string: its size including the terminating zero, then the bytes; size 0 is "no string".
    std::expected<std::string, Error> string() {
        auto size = m_reader.readU32Le();
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        if (*size == 0) {
            return std::string();
        }
        auto bytes = m_reader.readBytes(*size);
        if (!bytes) {
            return std::unexpected(std::move(bytes.error()));
        }
        // The stored size counts the terminator, which is not part of the string.
        std::string text(*size - 1, '\0');
        for (std::size_t i = 0; i + 1 < *size; ++i) {
            text[i] = static_cast<char>((*bytes)[i]);
        }
        return text;
    }

    // Checks that `n` items of at least `itemSize` bytes each can still be present, so a corrupt count fails before
    // anything is allocated for it.
    std::expected<void, Error> fits(std::int32_t n, std::size_t itemSize) const {
        if (m_reader.remaining() / itemSize < static_cast<std::size_t>(n)) {
            return fail(ErrorCode::Truncated, "Lua chunk: shorter than a count in it says");
        }
        return {};
    }

    io::Reader& raw() { return m_reader; }

  private:
    io::Reader m_reader;
};

// Reads one function and, recursively, the functions nested in it, as Lua 4.0's loader does: source, line defined,
// parameter count, vararg flag, stack size, locals, line info, constants (strings, numbers, functions) and code.
std::expected<LuaProto, Error> readFunction(ChunkReader& in, int depth) {
    if (depth > kMaxLuaNesting) {
        return fail(ErrorCode::Invalid, "Lua chunk: functions nested too deeply");
    }
    LuaProto proto;
    // The fixed part: source name, line defined (unused), parameters, vararg flag, stack size.
    auto source = in.string();
    if (!source) {
        return std::unexpected(std::move(source.error()));
    }
    proto.source = std::move(*source);
    if (auto lineDefined = in.count(); !lineDefined) {
        return std::unexpected(std::move(lineDefined.error()));
    }
    auto numParams = in.count();
    if (!numParams) {
        return std::unexpected(std::move(numParams.error()));
    }
    proto.numParams = *numParams;
    auto vararg = in.raw().readU8();
    if (!vararg) {
        return std::unexpected(std::move(vararg.error()));
    }
    proto.isVararg = *vararg != 0;
    auto maxStack = in.count();
    if (!maxStack) {
        return std::unexpected(std::move(maxStack.error()));
    }
    proto.maxStackSize = *maxStack;

    // Debug information, read past: local variables (name, first and last pc) and one line number per instruction.
    auto localCount = in.count();
    if (!localCount) {
        return std::unexpected(std::move(localCount.error()));
    }
    if (auto fits = in.fits(*localCount, 12); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    for (std::int32_t i = 0; i < *localCount; ++i) {
        if (auto name = in.string(); !name) {
            return std::unexpected(std::move(name.error()));
        }
        // The first and last instruction where the local is live.
        if (auto range = in.raw().readBytes(8); !range) {
            return std::unexpected(std::move(range.error()));
        }
    }
    auto lineCount = in.count();
    if (!lineCount) {
        return std::unexpected(std::move(lineCount.error()));
    }
    if (auto fits = in.fits(*lineCount, 4); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    if (auto skipped = in.raw().readBytes(static_cast<std::size_t>(*lineCount) * 4); !skipped) {
        return std::unexpected(std::move(skipped.error()));
    }

    // Constants: strings, numbers, then nested functions.
    auto stringCount = in.count();
    if (!stringCount) {
        return std::unexpected(std::move(stringCount.error()));
    }
    if (auto fits = in.fits(*stringCount, 4); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    proto.strings.reserve(static_cast<std::size_t>(*stringCount));
    for (std::int32_t i = 0; i < *stringCount; ++i) {
        auto text = in.string();
        if (!text) {
            return std::unexpected(std::move(text.error()));
        }
        proto.strings.push_back(std::move(*text));
    }
    auto numberCount = in.count();
    if (!numberCount) {
        return std::unexpected(std::move(numberCount.error()));
    }
    if (auto fits = in.fits(*numberCount, kNumberSize); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    proto.numbers.reserve(static_cast<std::size_t>(*numberCount));
    for (std::int32_t i = 0; i < *numberCount; ++i) {
        proto.numbers.push_back(in.number().value()); // fits() covered these reads
    }
    auto protoCount = in.count();
    if (!protoCount) {
        return std::unexpected(std::move(protoCount.error()));
    }
    // A function takes at least 26 bytes, which bounds the reservation.
    if (auto fits = in.fits(*protoCount, 26); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    proto.protos.reserve(static_cast<std::size_t>(*protoCount));
    for (std::int32_t i = 0; i < *protoCount; ++i) {
        auto nested = readFunction(in, depth + 1);
        if (!nested) {
            return std::unexpected(std::move(nested.error()));
        }
        proto.protos.push_back(std::move(*nested));
    }

    // The code.
    auto codeCount = in.count();
    if (!codeCount) {
        return std::unexpected(std::move(codeCount.error()));
    }
    if (auto fits = in.fits(*codeCount, 4); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    proto.code.reserve(static_cast<std::size_t>(*codeCount));
    for (std::int32_t i = 0; i < *codeCount; ++i) {
        proto.code.push_back(in.raw().readU32Le().value()); // fits() covered these reads
    }
    return proto;
}

} // namespace

std::expected<LuaProto, Error> parseLuaChunk(std::span<const std::byte> data) {
    ChunkReader in(data);
    // The header: signature, version and the compiling machine's layout, then the test number.
    for (const std::uint8_t expected : kHeader) {
        auto byte = in.raw().readU8();
        if (!byte) {
            return std::unexpected(std::move(byte.error()));
        }
        if (*byte != expected) {
            return fail(ErrorCode::Invalid,
                        std::format("not a Lua 4.0 chunk for the game's layout (header byte {:#04x}, expected {:#04x})",
                                    *byte, expected));
        }
    }
    auto numberSize = in.raw().readU8();
    if (!numberSize) {
        return std::unexpected(std::move(numberSize.error()));
    }
    if (*numberSize != kNumberSize) {
        return fail(ErrorCode::Invalid, std::format("Lua chunk: {}-byte numbers, expected 8", *numberSize));
    }
    auto test = in.number();
    if (!test) {
        return std::unexpected(std::move(test.error()));
    }
    if (*test != kTestNumber) {
        return fail(ErrorCode::Invalid, "Lua chunk: the test number does not match (another number format)");
    }

    // The main function, which must be all that follows.
    auto main = readFunction(in, 0);
    if (!main) {
        return main;
    }
    if (in.raw().remaining() != 0) {
        return fail(ErrorCode::Invalid,
                    std::format("Lua chunk: {} bytes after the main function", in.raw().remaining()));
    }
    return main;
}

} // namespace coney::script
