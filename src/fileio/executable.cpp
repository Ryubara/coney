// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/executable.h"

#include <algorithm>
#include <array>
#include <format>

#include "fileio/reader.h"

namespace coney::io {

namespace {

// The ELF identification and header fields used here (the public ELF32 layout).
constexpr std::array<std::byte, 4> kElfMagic{std::byte{0x7f}, std::byte{'E'}, std::byte{'L'}, std::byte{'F'}};
constexpr std::uint8_t kElfClass32 = 1;
constexpr std::uint8_t kElfDataLittle = 1;
constexpr std::size_t kClassOffset = 4;
constexpr std::size_t kDataOffset = 5;
constexpr std::size_t kProgramHeaderOffset = 0x1c; // e_phoff
constexpr std::size_t kProgramHeaderSize = 0x2a;   // e_phentsize, then e_phnum
constexpr std::uint32_t kLoadSegment = 1;          // PT_LOAD
constexpr std::size_t kMinProgramHeader = 0x20;    // the ELF32 program header's size

// One loadable segment's file bytes and where they load.
struct Segment {
    std::uint32_t offset = 0;
    std::uint32_t address = 0;
    std::uint32_t fileSize = 0;
};

// Reads the program header at `at`.
std::expected<Segment, Error> readSegment(Reader& reader, std::size_t at, std::uint32_t& type) {
    if (auto seeked = reader.seek(at); !seeked) {
        return std::unexpected(seeked.error());
    }
    auto pType = reader.readU32Le();
    auto offset = reader.readU32Le();
    auto vaddr = reader.readU32Le();
    auto paddr = reader.readU32Le();
    auto fileSize = reader.readU32Le();
    if (!pType || !offset || !vaddr || !paddr || !fileSize) {
        return fail(ErrorCode::Truncated, "executable: a program header runs past the end of the file");
    }
    type = *pType;
    return Segment{.offset = *offset, .address = *vaddr, .fileSize = *fileSize};
}

} // namespace

std::expected<std::vector<std::uint32_t>, Error> readExecutableWords(std::span<const std::byte> elf,
                                                                     std::uint32_t address, std::size_t count) {
    // The identification: a 32-bit little-endian ELF file.
    if (elf.size() < kProgramHeaderSize + 4 || !std::equal(kElfMagic.begin(), kElfMagic.end(), elf.begin()) ||
        std::to_integer<std::uint8_t>(elf[kClassOffset]) != kElfClass32 ||
        std::to_integer<std::uint8_t>(elf[kDataOffset]) != kElfDataLittle) {
        return fail(ErrorCode::Invalid, "executable: not a 32-bit little-endian ELF file");
    }
    Reader reader(elf);
    if (auto seeked = reader.seek(kProgramHeaderOffset); !seeked) {
        return std::unexpected(seeked.error());
    }
    const auto headers = reader.readU32Le();
    if (!headers || !reader.seek(kProgramHeaderSize)) {
        return fail(ErrorCode::Truncated, "executable: the header is cut short");
    }
    const auto entrySize = reader.readU16Le();
    const auto entries = reader.readU16Le();
    if (!entrySize || !entries || *entrySize < kMinProgramHeader) {
        return fail(ErrorCode::Invalid, "executable: bad program header table");
    }

    // The loadable segment whose file bytes hold the whole range.
    const std::uint64_t bytes = static_cast<std::uint64_t>(count) * 4U;
    for (std::size_t i = 0; i < *entries; ++i) {
        std::uint32_t type = 0;
        auto segment = readSegment(reader, *headers + (i * *entrySize), type);
        if (!segment) {
            return std::unexpected(segment.error());
        }
        if (type != kLoadSegment || address < segment->address ||
            address + bytes > static_cast<std::uint64_t>(segment->address) + segment->fileSize) {
            continue;
        }
        if (auto seeked = reader.seek(static_cast<std::size_t>(segment->offset) + (address - segment->address));
            !seeked) {
            return std::unexpected(seeked.error());
        }
        std::vector<std::uint32_t> words;
        words.reserve(count);
        for (std::size_t w = 0; w < count; ++w) {
            auto word = reader.readU32Le();
            if (!word) {
                return std::unexpected(word.error());
            }
            words.push_back(*word);
        }
        return words;
    }
    return fail(ErrorCode::Invalid, std::format("executable: no loadable segment holds 0x{:08x}", address));
}

std::expected<std::vector<std::uint32_t>, Error> readExecutableWords(const Disc& disc, std::string_view name,
                                                                     std::uint32_t address, std::size_t count) {
    auto file = disc.readFile(name);
    if (!file) {
        return std::unexpected(file.error());
    }
    return readExecutableWords(*file, address, count);
}

} // namespace coney::io
