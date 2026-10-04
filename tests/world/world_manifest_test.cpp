// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/world_manifest.h"

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"

using coney::ErrorCode;
using coney::test::Bytes;
using coney::world::readWorldManifest;

TEST_CASE("a manifest gives the world's sizes and each part's", "[world_manifest]") {
    // worldSize 1000, worldHeapSize 1400, two parts {300, 390} and {500, 650}.
    Bytes data;
    data.u32(1000).u32(1400).u32(2).u32(300).u32(390).u32(500).u32(650);
    auto manifest = readWorldManifest(data.span());
    REQUIRE(manifest.has_value());
    CHECK(manifest->worldSize == 1000);
    CHECK(manifest->worldHeapSize == 1400);
    REQUIRE(manifest->parts.size() == 2);
    CHECK(manifest->parts[0].fileSize == 300);
    CHECK(manifest->parts[0].heapSize == 390);
    CHECK(manifest->parts[1].fileSize == 500);
    CHECK(manifest->parts[1].heapSize == 650);
}

TEST_CASE("a manifest shorter than its count or with bytes after it is refused", "[world_manifest]") {
    Bytes shortHeader;
    shortHeader.u32(1).u32(2);
    CHECK(readWorldManifest(shortHeader.span()).error().code == ErrorCode::Truncated);

    // Three parts announced, one given.
    Bytes shortParts;
    shortParts.u32(1).u32(2).u32(3).u32(4).u32(5);
    CHECK(readWorldManifest(shortParts.span()).error().code == ErrorCode::Truncated);

    // One part and a stray word.
    Bytes trailing;
    trailing.u32(1).u32(2).u32(1).u32(4).u32(5).u32(6);
    CHECK(readWorldManifest(trailing.span()).error().code == ErrorCode::Invalid);
}

TEST_CASE("without a manifest the heaps are measured from the files", "[world_manifest]") {
    // 135 % with a floor of 0xaf000 for the world, 130 % with a floor of 256 KB for a part.
    CHECK(coney::world::worldHeapWithoutManifest(100) == 0xaf000);
    CHECK(coney::world::worldHeapWithoutManifest(1'000'000) == 1'350'000);
    CHECK(coney::world::partHeapWithoutManifest(100) == 256 * 1024);
    CHECK(coney::world::partHeapWithoutManifest(1'000'000) == 1'300'000);
}
