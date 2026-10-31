// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/wad.h"

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "fileio/disc.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::io::Disc;
using coney::io::Wad;
using coney::test::Bytes;
using coney::test::TempDir;
using coney::test::twoEntryDir;

TEST_CASE("Wad opens a disc folder, looks entries up and reads them", "[wad]") {
    // Expected: formats/wad-dir.md#name-lookup, confirmed (code)
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
    // Expected: formats/wad-dir.md#coneys-implementation, Coney's own check
    TempDir folder;
    folder.write("WARRIORS.DIR", twoEntryDir().span());
    auto disc = Disc::open(folder.path());
    REQUIRE(disc.has_value());
    auto wad = Wad::open(std::move(*disc));
    REQUIRE_FALSE(wad.has_value());
    CHECK(wad.error().code == ErrorCode::NotFound);
}

namespace {

// A disc folder of twoEntryDir()'s archive: global.lua's 5 bytes "hello" at 0, level1.lev's 12 at 2048.
void writeTwoEntryDisc(const TempDir& folder) {
    folder.write("WARRIORS.DIR", twoEntryDir().span());
    Bytes wadData;
    wadData.text("hello").padTo(2048).u32(1).u32(2).u32(3);
    folder.write("WARRIORS.WAD", wadData.span());
}

} // namespace

TEST_CASE("Wad reads an entry from an overrides folder in place of the archive's", "[wad]") {
    // Expected: docs/guides/asset-and-script-architecture.md#file-overrides, Coney's own behaviour
    TempDir disc;
    writeTwoEntryDisc(disc);
    TempDir overrides;
    // A longer file than the entry it replaces, named in another letter case, and one that matches no entry.
    Bytes edited;
    edited.text("edited script");
    overrides.write("GLOBAL.LUA", edited.span());
    overrides.write("nope.lua", edited.span());
    auto opened = Disc::open(disc.path());
    REQUIRE(opened.has_value());
    auto wad = Wad::open(std::move(*opened));
    REQUIRE(wad.has_value());
    CHECK_FALSE(wad->modded());

    auto report = wad->useOverrides(overrides.path());
    REQUIRE(report.has_value());
    CHECK(report->replaced == 1);
    CHECK(report->unmatched == std::vector<std::string>{"nope.lua"});
    CHECK(report->duplicates.empty());
    CHECK(wad->modded());

    // The replaced entry reads the file whole, at its own size; the other entry still reads the archive.
    auto global = wad->lookup("global.lua");
    REQUIRE(global.has_value());
    auto stream = wad->openEntry(**global);
    REQUIRE(stream.has_value());
    CHECK(stream->size() == 13);
    auto level1 = wad->lookup("level1.lev");
    REQUIRE(level1.has_value());
    auto untouched = wad->openEntry(**level1);
    REQUIRE(untouched.has_value());
    CHECK(untouched->size() == 12);
    CHECK(untouched->readU32Le() == 1U);
}

TEST_CASE("Wad treats a missing overrides folder as no overrides", "[wad]") {
    // Expected: docs/guides/asset-and-script-architecture.md#file-overrides, Coney's own behaviour
    TempDir disc;
    writeTwoEntryDisc(disc);
    auto opened = Disc::open(disc.path());
    REQUIRE(opened.has_value());
    auto wad = Wad::open(std::move(*opened));
    REQUIRE(wad.has_value());
    // An install without the folder plays the disc as it is: no error, nothing replaced.
    auto none = wad->useOverrides(disc.path() / "overrides" / "files");
    REQUIRE(none.has_value());
    CHECK(none->replaced == 0);
    CHECK_FALSE(wad->modded());

    // A missing folder is no reset either: the entries an earlier folder replaced stay replaced.
    TempDir overrides;
    Bytes edited;
    edited.text("edited script");
    overrides.write("global.lua", edited.span());
    REQUIRE(wad->useOverrides(overrides.path()).has_value());
    auto again = wad->useOverrides(disc.path() / "overrides" / "files");
    REQUIRE(again.has_value());
    CHECK(again->replaced == 0);
    CHECK(wad->modded());
    auto global = wad->lookup("global.lua");
    REQUIRE(global.has_value());
    auto stream = wad->openEntry(**global);
    REQUIRE(stream.has_value());
    CHECK(stream->size() == 13);
}

TEST_CASE("Wad takes the first name in order when two override files name one entry", "[wad]") {
    // Expected: docs/guides/asset-and-script-architecture.md#file-overrides, Coney's own behaviour
    TempDir disc;
    writeTwoEntryDisc(disc);
    TempDir overrides;
    // global.lua by its name hash and by its name: "0x..." sorts first, so it is used and the name is the duplicate.
    Bytes byHash;
    byHash.u32(7);
    overrides.write("0x7e23a6f2", byHash.span());
    Bytes byName;
    byName.text("edited script");
    overrides.write("global.lua", byName.span());
    auto opened = Disc::open(disc.path());
    REQUIRE(opened.has_value());
    auto wad = Wad::open(std::move(*opened));
    REQUIRE(wad.has_value());

    auto report = wad->useOverrides(overrides.path());
    REQUIRE(report.has_value());
    CHECK(report->replaced == 1);
    CHECK(report->duplicates == std::vector<std::string>{"global.lua"});
    auto global = wad->lookup("global.lua");
    REQUIRE(global.has_value());
    auto stream = wad->openEntry(**global);
    REQUIRE(stream.has_value());
    CHECK(stream->size() == 4);
    CHECK(stream->readU32Le() == 7U);
}

namespace {

// A disc folder whose archive holds a script: level99.lua, an 8-byte Lua 4.0 chunk (ESC "Lua", version 0x40) at 0,
// and level1.lev's 12 bytes at 2048.
void writeScriptDisc(const TempDir& folder) {
    Bytes dir;
    dir.header(2, 0, 0, 0);
    dir.u32(0).u32(8).u32(coney::nameHash("./ee_files/level99.lua"));
    dir.u32(2048).u32(12).u32(coney::nameHash("./ee_files/level1.lev"));
    folder.write("WARRIORS.DIR", dir.span());
    Bytes wadData;
    wadData.u8(0x1b).text("Lua").u8(0x40).u8(1).u8(2).u8(3).padTo(2048).u32(1).u32(2).u32(3);
    folder.write("WARRIORS.WAD", wadData.span());
}

} // namespace

TEST_CASE("Wad refuses overrides that would put a script in place of the disc's", "[wad]") {
    // Expected: docs/guides/studio/security.md#surface (Bytecode, Lua 4.0), Coney's own rule
    TempDir disc;
    writeScriptDisc(disc);
    TempDir overrides;
    // Lua source in place of the disc's chunk, and a compiled chunk in place of a data entry.
    Bytes source;
    source.text("HuCreate(\"Cleon\", 2)");
    overrides.write("level99.lua", source.span());
    Bytes chunk;
    chunk.u8(0x1b).text("Lua").u8(0x40).u32(0);
    overrides.write("level1.lev", chunk.span());
    auto opened = Disc::open(disc.path());
    REQUIRE(opened.has_value());
    auto wad = Wad::open(std::move(*opened));
    REQUIRE(wad.has_value());

    auto report = wad->useOverrides(overrides.path());
    REQUIRE(report.has_value());
    CHECK(report->replaced == 0);
    CHECK(report->scripts == std::vector<std::string>{"level1.lev", "level99.lua"});
    CHECK_FALSE(wad->modded());

    // Both entries still read the disc: the player's own chunk, and the level's bytes.
    auto script = wad->lookup("level99.lua");
    REQUIRE(script.has_value());
    auto stream = wad->openEntry(**script);
    REQUIRE(stream.has_value());
    CHECK(stream->size() == 8);
    CHECK(stream->readU32Le() == 0x61754c1bU); // ESC "Lua"
    auto level1 = wad->lookup("level1.lev");
    REQUIRE(level1.has_value());
    auto untouched = wad->openEntry(**level1);
    REQUIRE(untouched.has_value());
    CHECK(untouched->size() == 12);
    CHECK(untouched->readU32Le() == 1U);
}
