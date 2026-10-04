// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/particle_page.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"
#include "support/sheet_fixtures.h"

using coney::ErrorCode;
using coney::graphics::parseParticlePage;
using coney::graphics::UvRect;
using coney::test::Bytes;
using coney::test::particlePage;

TEST_CASE("a particle page reads its rectangles and first glyph", "[particle_page]") {
    const Bytes data = particlePage(-1, {UvRect{0.0F, 0.0F, 0.5F, 0.75F}, UvRect{0.5F, 0.25F, 1.0F, 1.0F}});
    CHECK(data.size() == 64); // 0x14 + 2 * 16 = 52, padded to 64
    auto parsed = parseParticlePage(data.span());
    REQUIRE(parsed.has_value());
    CHECK(parsed->firstGlyph == -1);
    REQUIRE(parsed->rects.size() == 2);
    CHECK(parsed->rect(0) == UvRect{0.0F, 0.0F, 0.5F, 0.75F});
    CHECK(parsed->rect(1) == UvRect{0.5F, 0.25F, 1.0F, 1.0F});
}

TEST_CASE("a font page keeps its first glyph", "[particle_page]") {
    auto parsed = parseParticlePage(particlePage(94, {UvRect{}}).span());
    REQUIRE(parsed.has_value());
    CHECK(parsed->firstGlyph == 94);
}

TEST_CASE("a particle page shorter than its header or its count is refused", "[particle_page]") {
    Bytes header;
    header.u32(0).u32(1).u32(0);
    auto shortHeader = parseParticlePage(header.span());
    REQUIRE_FALSE(shortHeader.has_value());
    CHECK(shortHeader.error().code == ErrorCode::Truncated);

    // A count of three with room for two.
    Bytes data = particlePage(-1, {UvRect{}, UvRect{}});
    data.patchU32(4, 3);
    auto tooMany = parseParticlePage(data.span().first(0x14 + 2 * 16));
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().code == ErrorCode::Truncated);

    // A huge count is refused before anything is allocated.
    data.patchU32(4, 0xFFFFFFFF);
    CHECK_FALSE(parseParticlePage(data.span()).has_value());
}
