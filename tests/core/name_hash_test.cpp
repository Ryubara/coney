// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/name_hash.h"

#include <array>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

using coney::crc32;
using coney::nameHash;

TEST_CASE("crc32 matches the standard check value", "[name_hash]") {
    // The CRC-32 check value every implementation agrees on (zlib.crc32(b"123456789")).
    CHECK(crc32(std::string_view("123456789")) == 0xCBF43926U);
    CHECK(crc32(std::string_view("")) == 0U);
    const std::array<std::byte, 3> bytes{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    CHECK(crc32(std::span<const std::byte>(bytes)) == crc32(std::string_view("abc")));
}

TEST_CASE("crc32 of package is the pack marker", "[name_hash]") {
    // docs/research/formats/wad-contents.md: a pack's header ends with crc32("package").
    CHECK(crc32(std::string_view("package")) == 0xDE686795U);
}

TEST_CASE("nameHash lowercases before hashing", "[name_hash]") {
    // The example in docs/research/formats/wad-dir.md.
    CHECK(nameHash("./ee_files/global.lua") == 0x7e23a6f2U);
    CHECK(nameHash("./EE_FILES/Global.LUA") == 0x7e23a6f2U);
    CHECK(crc32(std::string_view("./EE_FILES/Global.LUA")) != 0x7e23a6f2U);
}

TEST_CASE("lowercaseAscii only changes A to Z", "[name_hash]") {
    CHECK(coney::lowercaseAscii("AbZ_09.\xC4") == "abz_09.\xC4");
}
