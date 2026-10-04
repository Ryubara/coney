// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/wad.h"

#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "fileio/disc.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::nameHash;
using coney::io::Disc;
using coney::io::Wad;
using coney::test::Bytes;
using coney::test::TempDir;

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

TEST_CASE("Wad opens a disc folder, looks entries up and reads them", "[wad]") {
    TempDir folder;
    folder.write("WARRIORS.DIR", twoEntryDir().span());
    // The archive the index describes: global.lua's 5 bytes at 0, level1.lev's 12 at 2048.
    Bytes wadData;
    wadData.text("hello").padTo(2048).u32(1).u32(2).u32(3);
    folder.write("WARRIORS.WAD", wadData.span());
    auto disc = Disc::open(folder.path());
    REQUIRE(disc.has_value());
    auto wad = Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    auto byName = wad->lookup("level1.lev");
    REQUIRE(byName.has_value());
    auto byHash = wad->lookup("0x7E23A6F2");
    REQUIRE(byHash.has_value());
    CHECK((*byHash)->index == 0);
    CHECK(wad->lookup("0x").error().code == ErrorCode::InvalidArgument);
    CHECK(wad->lookup("0x123456789").error().code == ErrorCode::InvalidArgument);
    CHECK(wad->lookup("0xg").error().code == ErrorCode::InvalidArgument);
    CHECK(wad->lookup("0x1").error().code == ErrorCode::NotFound);
    CHECK(wad->lookup("nope.lev").error().code == ErrorCode::NotFound);

    auto stream = wad->openEntry(**byName);
    REQUIRE(stream.has_value());
    CHECK(stream->size() == 12);
    CHECK(stream->readU32Le() == 1U);
}

TEST_CASE("Wad refuses a disc without the archive", "[wad]") {
    TempDir folder;
    folder.write("WARRIORS.DIR", twoEntryDir().span());
    auto disc = Disc::open(folder.path());
    REQUIRE(disc.has_value());
    auto wad = Wad::open(std::move(*disc));
    REQUIRE_FALSE(wad.has_value());
    CHECK(wad.error().code == ErrorCode::NotFound);
}
