// SPDX-License-Identifier: GPL-3.0-or-later
// The scene reader over the WAD (scenes/scene_disc.h), on a synthetic disc folder.

#include <cstddef>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "scenes/scene_disc.h"
#include "support/fixtures.h"

using coney::io::Disc;
using coney::io::Wad;
using coney::test::Bytes;
using coney::test::TempDir;

TEST_CASE("the scene reader reads a replaced .scn entry at the override's size", "[scenes][wad]") {
    // Expected: docs/guides/asset-and-script-architecture.md#file-overrides, Coney's own behaviour
    // A disc whose archive holds intro.scn, 8 bytes at 0; the record's contents do not matter to the reader.
    TempDir disc;
    Bytes dir;
    dir.header(1, 0, 0, 0);
    dir.u32(0).u32(8).u32(coney::nameHash("./ee_files/intro.scn"));
    disc.write("WARRIORS.DIR", dir.span());
    Bytes wadData;
    wadData.u32(1).u32(2);
    disc.write("WARRIORS.WAD", wadData.span());
    // A data replacement longer than the entry: read whole, not cut at the 8 bytes WARRIORS.DIR lists.
    TempDir overrides;
    Bytes edited;
    edited.u32(7).u32(8).u32(9);
    overrides.write("intro.scn", edited.span());
    auto opened = Disc::open(disc.path());
    REQUIRE(opened.has_value());
    auto wad = Wad::open(std::move(*opened));
    REQUIRE(wad.has_value());
    auto report = wad->useOverrides(overrides.path());
    REQUIRE(report.has_value());
    REQUIRE(report->replaced == 1);

    auto record = coney::scenes::wadSceneSource(*wad)("intro");
    REQUIRE(record.has_value());
    CHECK(record->size() == std::size_t{12});
    CHECK(record->at(8) == std::byte{9});
}
