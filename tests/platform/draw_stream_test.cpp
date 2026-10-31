// SPDX-License-Identifier: GPL-3.0-or-later
// The draw stream's grid and hash (docs/engine/renderer.md#comparing): Coney's own check, with no counterpart in the
// original, so every expected value here follows from the definition on that page (1/1024 of a unit for positions
// and texture coordinates, 8 bits a colour channel, 64-bit FNV-1a over little-endian bytes).
#include "platform/draw_stream.h"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::platform::drawColourChannel;
using coney::platform::drawGridValue;
using coney::platform::DrawRecord;
using coney::platform::DrawStream;
using coney::platform::pixelDigest;

namespace {

// 64-bit FNV-1a written out again, so the test checks the stream against the definition and not against itself.
std::uint64_t referenceFnv(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// A one-record stream holding a single grid value.
std::uint64_t hashOfGridValue(float value) {
    DrawStream stream;
    stream.begin(DrawRecord::Im2DDraw);
    stream.gridValue(value);
    return stream.hash();
}

} // namespace

TEST_CASE("draw stream positions round to the nearest 1/1024 of a unit", "[platform][draw-stream]") {
    // Expected: the grid of docs/engine/renderer.md#comparing, Coney's own definition (no original counterpart).
    CHECK(drawGridValue(0.0F) == 0);
    CHECK(drawGridValue(1.0F) == 1024);
    CHECK(drawGridValue(-2.5F) == -2560);
    CHECK(drawGridValue(640.0F) == 655360);
    // Half a step rounds away from zero, on every system (std::llround).
    CHECK(drawGridValue(0.5F / 1024.0F) == 1);
    CHECK(drawGridValue(-0.5F / 1024.0F) == -1);
    CHECK(drawGridValue(0.49F / 1024.0F) == 0);
    // No input is undefined: NaN is a value of its own, infinities clamp at 2^40 steps.
    CHECK(drawGridValue(std::numeric_limits<float>::quiet_NaN()) == -(1LL << 41));
    CHECK(drawGridValue(std::numeric_limits<float>::infinity()) == (1LL << 40));
    CHECK(drawGridValue(-std::numeric_limits<float>::infinity()) == -(1LL << 40));
}

TEST_CASE("draw stream colours round to 8 bits a channel", "[platform][draw-stream]") {
    // Expected: 8 bits a channel, docs/engine/renderer.md#comparing.
    CHECK(drawColourChannel(0.0F) == 0);
    CHECK(drawColourChannel(1.0F) == 255);
    CHECK(drawColourChannel(0.5F) == 128);
    CHECK(drawColourChannel(-1.0F) == 0);
    CHECK(drawColourChannel(2.0F) == 255);
    CHECK(drawColourChannel(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("a draw stream hashes its records' bytes little endian with FNV-1a", "[platform][draw-stream]") {
    // Expected: the encoding of docs/engine/renderer.md#comparing, rebuilt byte by byte here.
    CHECK(DrawStream{}.hash() == referenceFnv({}));
    DrawStream stream;
    stream.begin(DrawRecord::Im3DDraw);
    stream.word(0x01020304U);
    stream.gridValue(1.0F); // 1024 as eight bytes
    stream.colour(10, 20, 30, 40);
    stream.digest(0x1122334455667788ULL);
    const std::vector<std::uint8_t> bytes{2,  0x04, 0x03, 0x02, 0x01, 0x00, 0x04, 0,    0,    0,    0,    0,   0,
                                          10, 20,   30,   40,   0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    CHECK(stream.hash() == referenceFnv(bytes));
    REQUIRE(stream.recordHashes().size() == 1);
    CHECK(stream.recordHashes().front() == referenceFnv(bytes));
    CHECK(pixelDigest(std::span<const std::uint8_t>(bytes)) == referenceFnv(bytes));
}

TEST_CASE("values within the grid's step hash the same and a step apart differ", "[platform][draw-stream]") {
    // Expected: the grid's purpose, docs/engine/renderer.md#comparing: the last bits of a float do not move the hash.
    const float base = 100.0F;
    CHECK(hashOfGridValue(base) == hashOfGridValue(base + 0.0001F));
    CHECK(hashOfGridValue(base) == hashOfGridValue(base - 0.0004F));
    CHECK(hashOfGridValue(base) != hashOfGridValue(base + 1.0F / 1024.0F));
    CHECK(hashOfGridValue(base) != hashOfGridValue(-base));
}

TEST_CASE("a draw stream counts its records by kind and hashes each one", "[platform][draw-stream]") {
    // Expected: Coney's own counts for the line `--draw-hash` prints (docs/engine/renderer.md#comparing).
    DrawStream stream;
    stream.begin(DrawRecord::CameraClear);
    stream.begin(DrawRecord::Im2DDraw);
    stream.countVertices(4);
    stream.begin(DrawRecord::Im3DDraw);
    stream.countVertices(36);
    stream.begin(DrawRecord::RasterUpload);
    stream.begin(DrawRecord::FrameCopy);
    stream.begin(DrawRecord::CameraShow);
    const auto& counts = stream.counts();
    CHECK(counts.records == 6);
    CHECK(counts.im2dDraws == 1);
    CHECK(counts.im3dDraws == 1);
    CHECK(counts.vertices == 40);
    CHECK(counts.cameraOps == 2);
    CHECK(counts.rasterOps == 1);
    CHECK(counts.frameOps == 1);
    const std::vector<std::uint64_t> records = stream.recordHashes();
    REQUIRE(records.size() == 6);
    // Each record holds only its kind's byte, so each record's hash is that one byte's.
    CHECK(records[0] == referenceFnv({3}));
    CHECK(records[5] == referenceFnv({6}));
}
