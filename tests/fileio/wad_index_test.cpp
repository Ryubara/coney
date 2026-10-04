// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/wad_index.h"

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::nameHash;
using coney::io::WadIndex;
using coney::test::Bytes;

namespace {

/// A WARRIORS.DIR with two entries: global.lua at 0 (5 bytes) and level1.lev at 2048 (12 bytes).
Bytes twoEntryDir() {
    Bytes dir;
    dir.header(2, 0, 0, 0);
    dir.u32(0).u32(5).u32(nameHash("./ee_files/global.lua"));
    dir.u32(2048).u32(12).u32(nameHash("./ee_files/level1.lev"));
    return dir;
}

} // namespace

TEST_CASE("wadPath adds the ee_files prefix to bare names only", "[wad_index]") {
    CHECK(coney::io::wadPath("level1.lev") == "./ee_files/level1.lev");
    CHECK(coney::io::wadPath("./ee_files/x") == "./ee_files/x");
}

TEST_CASE("WARRIORS.DIR parses and finds entries by name and hash", "[wad_index]") {
    const Bytes dir = twoEntryDir();
    auto index = WadIndex::parse(dir.span(), 2060);
    REQUIRE(index.has_value());
    REQUIRE(index->entries().size() == 2);
    const auto* level = index->find("LEVEL1.LEV");
    REQUIRE(level != nullptr);
    CHECK(level->index == 1);
    CHECK(level->wadOffset == 2048);
    CHECK(level->size == 12);
    CHECK(index->find("./ee_files/global.lua") == &index->entries()[0]);
    CHECK(index->findHash(0x7e23a6f2U) == &index->entries()[0]);
    CHECK(index->find("missing.txt") == nullptr);
}

TEST_CASE("a WARRIORS.DIR whose size disagrees with its count is refused", "[wad_index]") {
    Bytes shortDir = twoEntryDir();
    shortDir.patchU32(0, 3); // claims a third entry the file does not hold
    auto truncated = WadIndex::parse(shortDir.span());
    REQUIRE_FALSE(truncated.has_value());
    CHECK(truncated.error().code == ErrorCode::Truncated);

    Bytes longDir = twoEntryDir();
    longDir.patchU32(0, 1); // claims one entry but holds two
    auto extra = WadIndex::parse(longDir.span());
    REQUIRE_FALSE(extra.has_value());
    CHECK(extra.error().code == ErrorCode::Invalid);

    // A count whose expected size would overflow 32 bits, and a file shorter than the header.
    Bytes huge;
    huge.header(0xFFFFFFFFU, 0, 0, 0);
    CHECK_FALSE(WadIndex::parse(huge.span()).has_value());
    CHECK_FALSE(WadIndex::parse(Bytes().u32(0).span()).has_value());
}

TEST_CASE("an entry past the end of the WAD is refused", "[wad_index]") {
    const Bytes dir = twoEntryDir();
    auto index = WadIndex::parse(dir.span(), 2059); // level1.lev ends at 2060
    REQUIRE_FALSE(index.has_value());
    CHECK(index.error().code == ErrorCode::Invalid);
}

TEST_CASE("on a duplicate hash the first entry wins", "[wad_index]") {
    Bytes dir;
    dir.header(2, 0, 0, 0);
    dir.u32(0).u32(1).u32(0x1234);
    dir.u32(2048).u32(1).u32(0x1234);
    auto index = WadIndex::parse(dir.span());
    REQUIRE(index.has_value());
    CHECK(index->findHash(0x1234)->index == 0);
}
