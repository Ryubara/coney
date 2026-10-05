// SPDX-License-Identifier: GPL-3.0-or-later
// Reading words at a virtual address of a PS2 executable through its program headers, on a synthetic ELF file built
// byte by byte here (the public ELF32 layout); nothing from the disc.
#include "fileio/executable.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"

namespace {

// A 32-bit little-endian ELF file with a non-loadable header first and one loadable segment: file offset 0x80, loaded
// at 0x00100000, `words` words of 0x1000 + i.
std::vector<std::byte> syntheticElf(std::uint32_t words) {
    coney::test::Bytes out;
    out.u8(0x7f).text("ELF").u8(1).u8(1).u8(1).padTo(0x1c);
    out.u32(0x34);                                               // e_phoff
    out.u32(0).u32(0).u16(0x34);                                 // e_shoff, e_flags, e_ehsize
    out.u16(0x20).u16(2);                                        // e_phentsize, e_phnum
    out.u16(0).u16(0).u16(0);                                    // section headers: none
    out.u32(4).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0); // a note: skipped
    out.u32(1).u32(0x80).u32(0x00100000).u32(0x00100000).u32(words * 4).u32(words * 4).u32(5).u32(16); // PT_LOAD
    out.padTo(0x80);
    for (std::uint32_t i = 0; i < words; ++i) {
        out.u32(0x1000 + i);
    }
    return out.data();
}

} // namespace

TEST_CASE("words at a virtual address are read from the loadable segment that holds them", "[executable]") {
    const std::vector<std::byte> elf = syntheticElf(16);
    auto words = coney::io::readExecutableWords(elf, 0x00100008, 4);
    REQUIRE(words.has_value());
    CHECK(words.value_or(std::vector<std::uint32_t>{}) == std::vector<std::uint32_t>{0x1002, 0x1003, 0x1004, 0x1005});
}

TEST_CASE("a range past the segment's file bytes, or a file that is not an ELF, fails", "[executable]") {
    const std::vector<std::byte> elf = syntheticElf(16);
    CHECK(coney::io::readExecutableWords(elf, 0x00100038, 4).error().code == coney::ErrorCode::Invalid);
    CHECK(coney::io::readExecutableWords(elf, 0x000ffffc, 1).error().code == coney::ErrorCode::Invalid);
    std::vector<std::byte> notElf = elf;
    notElf[1] = std::byte{'X'};
    CHECK(coney::io::readExecutableWords(notElf, 0x00100000, 1).error().code == coney::ErrorCode::Invalid);
    const std::vector<std::byte> cut(elf.begin(), elf.begin() + 0x40);
    CHECK(!coney::io::readExecutableWords(cut, 0x00100000, 1).has_value());
}
