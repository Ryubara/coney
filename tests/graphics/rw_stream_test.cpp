// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/rw_stream.h"

#include <cstddef>
#include <span>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"
#include "support/rw_fixtures.h"

using coney::ErrorCode;
using coney::graphics::detectHeaderedRwStream;
using coney::graphics::findRwSection;
using coney::graphics::inspectTexDictionary;
using coney::graphics::kRwExtension;
using coney::graphics::kRwLibraryStamp;
using coney::graphics::kRwStruct;
using coney::graphics::kRwTexDictionary;
using coney::test::Bytes;
using coney::test::ps2Texture;
using coney::test::rgba32Raster;
using coney::test::rgbaTexture2x2;
using coney::test::rwSection;
using coney::test::texDictionary;

TEST_CASE("a headered RenderWare stream is recognised by its header and stamp", "[rw_stream]") {
    Bytes entry;
    entry.header(1, 0, 0, 0xCAFE);
    entry.append(texDictionary({}).span());
    auto found = detectHeaderedRwStream(entry.span());
    REQUIRE(found.has_value());
    CHECK(found->id == 0xCAFE);
    CHECK(found->firstSection.id == kRwTexDictionary);
    CHECK(found->firstSection.libraryStamp == kRwLibraryStamp);
}

TEST_CASE("chunk containers and other stamps are not headered RenderWare streams", "[rw_stream]") {
    // A flat container of one chunk: its second word is the byte count, never 0.
    Bytes container;
    container.header(1, 16, 0, 0).header(0x2A, 16, 0, 0).fill(16, 0);
    CHECK_FALSE(detectHeaderedRwStream(container.span()).has_value());
    // The right header but a stream from another RenderWare version.
    Bytes older;
    older.header(1, 0, 0, 0).u32(kRwTexDictionary).u32(0).u32(0x1803FFFF);
    CHECK_FALSE(detectHeaderedRwStream(older.span()).has_value());
    // Too short to hold a section header after the 16 bytes.
    Bytes shortEntry;
    shortEntry.header(1, 0, 0, 0).u32(kRwTexDictionary);
    CHECK_FALSE(detectHeaderedRwStream(shortEntry.span()).has_value());
}

TEST_CASE("findRwSection skips the sections before the one asked for", "[rw_stream]") {
    Bytes value;
    value.u32(7);
    Bytes stream = rwSection(kRwStruct, value);
    stream.append(texDictionary({}).span());
    auto offset = findRwSection(stream.span(), kRwTexDictionary);
    REQUIRE(offset.has_value());
    CHECK(*offset == 16); // after the 12-byte header and 4 bytes of the struct
    CHECK(findRwSection(stream.span(), 0x10).error().code == ErrorCode::NotFound);
    // A section whose size runs past the end.
    Bytes cut;
    cut.u32(kRwStruct).u32(100).u32(kRwLibraryStamp);
    CHECK(findRwSection(cut.span(), kRwTexDictionary).error().code == ErrorCode::Truncated);
}

TEST_CASE("inspectTexDictionary describes a PS2 texture dictionary", "[rw_stream]") {
    const Bytes dictionary = texDictionary({rgbaTexture2x2("first"), rgbaTexture2x2("second")});
    Bytes stream = dictionary;
    stream.fill(8, 0xEE); // bytes after the dictionary are not its concern
    auto info = inspectTexDictionary(stream.span());
    REQUIRE(info.has_value());
    CHECK(info->deviceId == 6);
    CHECK(info->libraryStamp == kRwLibraryStamp);
    CHECK(info->streamBytes == dictionary.size());
    REQUIRE(info->textures.size() == 2);
    CHECK(info->textures[0].name == "first");
    CHECK(info->textures[1].name == "second");
    CHECK(info->textures[0].mask.empty());
    CHECK(info->textures[0].width == 2);
    CHECK(info->textures[0].height == 2);
    CHECK(info->textures[0].depth == 32);
    CHECK(info->textures[0].rasterFormat == 0x0504);
    CHECK(info->textures[0].version == 0);
    CHECK(info->textures[0].pixelBytes == 16);
    CHECK(info->textures[0].paletteBytes == 0);
}

TEST_CASE("inspectTexDictionary refuses what librw would mishandle", "[rw_stream]") {
    Bytes pixels;
    pixels.fill(16, 0);

    SECTION("a name longer than librw's 32-byte field") {
        const Bytes stream = texDictionary({ps2Texture(std::string(40, 'n'), rgba32Raster(2, 2), pixels.span())});
        CHECK(inspectTexDictionary(stream.span()).error().code == ErrorCode::Invalid);
    }
    SECTION("pixel data whose size disagrees with the raster header") {
        Bytes fewer;
        fewer.fill(12, 0);
        const Bytes stream = texDictionary({ps2Texture("t", rgba32Raster(2, 2), fewer.span())});
        CHECK(inspectTexDictionary(stream.span()).error().code == ErrorCode::Invalid);
    }
    SECTION("a raster format librw cannot convert from the PS2 (565)") {
        auto raster = rgba32Raster(2, 2);
        raster.rasterFormat = 0x0204;
        raster.depth = 16;
        const Bytes stream = texDictionary({ps2Texture("t", raster, pixels.span())});
        CHECK(inspectTexDictionary(stream.span()).error().code == ErrorCode::Invalid);
    }
    SECTION("an 8-bit palette on a 4-bit raster") {
        auto raster = rgba32Raster(2, 2);
        raster.rasterFormat = 0x2504;
        raster.depth = 4;
        const Bytes stream = texDictionary({ps2Texture("t", raster, pixels.span())});
        CHECK(inspectTexDictionary(stream.span()).error().code == ErrorCode::Invalid);
    }
    SECTION("a texture for another platform") {
        Bytes stream = texDictionary({rgbaTexture2x2("t")});
        // The platform word follows the dictionary's header (12), its struct (12 + 4), the texture native section's
        // header (12) and the platform struct's header (12).
        stream.patchU32(52, 8); // D3D8
        CHECK(inspectTexDictionary(stream.span()).error().code == ErrorCode::Invalid);
    }
    SECTION("more textures in the count than in the stream") {
        Bytes stream = texDictionary({rgbaTexture2x2("t")});
        stream.patchU32(24, 0x00060002); // count 2 (low half), device 6 (high half)
        CHECK_FALSE(inspectTexDictionary(stream.span()).has_value());
    }
    SECTION("no dictionary extension after the textures") {
        Bytes header;
        header.u16(0).u16(6);
        const Bytes stream = rwSection(kRwTexDictionary, rwSection(kRwStruct, header));
        CHECK(inspectTexDictionary(stream.span()).error().code == ErrorCode::Truncated);
    }
    SECTION("a dictionary cut short") {
        const Bytes whole = texDictionary({rgbaTexture2x2("t")});
        CHECK(inspectTexDictionary(whole.span().first(whole.size() - 20)).error().code == ErrorCode::Truncated);
    }
    SECTION("something that is not a dictionary") {
        const Bytes stream = rwSection(kRwExtension, Bytes{});
        CHECK(inspectTexDictionary(stream.span()).error().code == ErrorCode::Invalid);
    }
}

TEST_CASE("a world stream is a count followed by a texture dictionary", "[rw_stream]") {
    Bytes world;
    world.u32(3).append(texDictionary({}).span());
    CHECK(coney::graphics::isWorldStream(world.span()));
    // A sector atomics file has its dictionary at 16, not 4; a chunk container has a byte count at 4.
    Bytes headered;
    headered.header(1, 0, 0, 0).append(texDictionary({}).span());
    CHECK_FALSE(coney::graphics::isWorldStream(headered.span()));
    Bytes container;
    container.header(1, 16, 0, 0).header(0x2A, 16, 0, 0).fill(16, 0);
    CHECK_FALSE(coney::graphics::isWorldStream(container.span()));
    CHECK_FALSE(coney::graphics::isWorldStream(world.span().first(10)));
}
