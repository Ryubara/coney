// SPDX-License-Identifier: GPL-3.0-or-later
// The pickups' and lock-pickable doors' glints (docs/research/particles.md#glints): a triglint's three glints at their
// offsets and sizes, their blink, the 30 m visibility check every 60 ticks, and the owners' sync.
#include "effects/glints.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/game_random.h"

using Catch::Matchers::WithinAbs;
using coney::anim::Vec3;
using coney::effects::GlintOwner;
using coney::effects::Particle;
using coney::effects::Triglint;
using coney::effects::Triglints;

namespace {

// A camera that sees everything, and one that sees nothing.
bool seen(Vec3 /*point*/, float /*distance*/) { return true; }
bool unseen(Vec3 /*point*/, float /*distance*/) { return false; }

// Always the same draw, so the intervals are known.
std::uint32_t drawZero(std::uint32_t /*n*/) { return 0; }

} // namespace

TEST_CASE("a triglint's glints start off and turn on at their first update, 30 ticks on", "[effects][glints]") {
    Triglint triglint(Vec3{1.0F, 2.0F, 3.0F});
    CHECK(triglint.glints() == 3);
    std::vector<Particle> sprites;
    for (int t = 0; t < Triglint::kGlintFirstUpdate - 1; ++t) {
        triglint.tick(seen, drawZero);
    }
    triglint.addSprites(sprites);
    CHECK(sprites.empty());
    triglint.tick(seen, drawZero);
    triglint.addSprites(sprites);
    REQUIRE(sprites.size() == 3);
    // At the triglint's position plus the fixed offsets, sized × 0.15, white at alpha 128, part_page1 rectangle 41.
    CHECK_THAT(sprites[0].position.y, WithinAbs(2.043F, 1e-5F));
    CHECK_THAT(sprites[0].position.z, WithinAbs(3.067F, 1e-5F));
    CHECK_THAT(sprites[1].position.x, WithinAbs(0.929F, 1e-5F));
    CHECK_THAT(sprites[0].size, WithinAbs(0.195F, 1e-5F));
    CHECK_THAT(sprites[1].size, WithinAbs(0.1125F, 1e-5F));
    CHECK_THAT(sprites[2].size, WithinAbs(0.0975F, 1e-5F));
    CHECK(sprites[0].colour == 0xFFFFFF80U);
    CHECK(sprites[0].rect == 41);
    CHECK_FALSE(sprites[0].fades);
}

TEST_CASE("a glint blinks: on for one interval of 20 to 30 ticks, off for the next", "[effects][glints]") {
    Triglint triglint(Vec3{});
    for (int t = 0; t < Triglint::kGlintFirstUpdate; ++t) {
        triglint.tick(seen, drawZero);
    }
    const auto lit = [&triglint] {
        std::vector<Particle> sprites;
        triglint.addSprites(sprites);
        return sprites.size();
    };
    CHECK(lit() == 3);
    // A draw of 0 gives the shortest interval, 20 ticks.
    for (int t = 0; t < Triglint::kGlintIntervalMin - 1; ++t) {
        triglint.tick(seen, drawZero);
    }
    CHECK(lit() == 3);
    triglint.tick(seen, drawZero);
    CHECK(lit() == 0);
    // The longest: a draw of 10 (`Random_Int(10)` + 20) keeps it 30 ticks.
    const auto drawTen = [](std::uint32_t n) { return n; };
    triglint.tick(seen, drawTen); // still off for the 20-tick interval drawn before
    for (int t = 0; t < Triglint::kGlintIntervalMin - 2; ++t) {
        triglint.tick(seen, drawTen);
    }
    CHECK(lit() == 0);
    triglint.tick(seen, drawTen);
    CHECK(lit() == 3);
    for (int t = 0; t < 29; ++t) {
        triglint.tick(seen, drawTen);
    }
    CHECK(lit() == 3);
    triglint.tick(seen, drawTen);
    CHECK(lit() == 0);
}

TEST_CASE("a triglint no camera sees within 30 m loses its glints at its update and gets them back when seen",
          "[effects][glints]") {
    Triglint triglint(Vec3{});
    float asked = 0.0F;
    const auto unseenRecording = [&asked](Vec3 point, float distance) {
        asked = distance;
        return unseen(point, distance);
    };
    for (int t = 0; t < Triglint::kInterval - 1; ++t) {
        triglint.tick(unseenRecording, drawZero);
    }
    CHECK(triglint.glints() == 3); // its update is every 60 ticks
    triglint.tick(unseenRecording, drawZero);
    CHECK(triglint.glints() == 0);
    CHECK_THAT(asked, WithinAbs(30.0F, 1e-6F));
    for (int t = 0; t < Triglint::kInterval; ++t) {
        triglint.tick(seen, drawZero);
    }
    CHECK(triglint.glints() == 3);
}

TEST_CASE("the triglints follow their owners: made for a new one, moved, and removed when it goes",
          "[effects][glints]") {
    Triglints triglints;
    const std::array<GlintOwner, 2> two{GlintOwner{5.0, Vec3{1.0F, 0.0F, 0.0F}}, GlintOwner{6.0, Vec3{}}};
    triglints.sync(two);
    CHECK(triglints.count() == 2);
    triglints.tick(Triglint::kGlintFirstUpdate, seen);
    CHECK(triglints.sprites().size() == 6);
    // Owner 5 moves; owner 6 goes (a pickup taken, a door no longer pickable).
    const std::array<GlintOwner, 1> one{GlintOwner{5.0, Vec3{4.0F, 0.0F, 0.0F}}};
    triglints.sync(one);
    REQUIRE(triglints.count() == 1);
    REQUIRE(triglints.find(5.0) != nullptr);
    CHECK_THAT(triglints.find(5.0)->position().x, WithinAbs(4.0F, 1e-6F));
    CHECK(triglints.find(6.0) == nullptr);
    // The kept triglint keeps its blink: its glints are still on.
    CHECK(triglints.sprites().size() == 3);
    triglints.clear();
    CHECK(triglints.count() == 0);
}

TEST_CASE("the blink draws Random_Int(10) from the game's table: a raw 10 keeps a glint on for 30 ticks",
          "[effects][glints]") {
    // A synthetic table whose every number gives 10 mod 11: the longest interval.
    std::vector<std::uint32_t> table(coney::GameRandom::kTableSize, 21U);
    Triglints triglints;
    triglints.setTable(table);
    const std::array<GlintOwner, 1> one{GlintOwner{1.0, Vec3{}}};
    triglints.sync(one);
    triglints.tick(Triglint::kGlintFirstUpdate, seen);
    CHECK(triglints.sprites().size() == 3);
    triglints.tick(Triglint::kGlintIntervalMin + 9, seen);
    CHECK(triglints.sprites().size() == 3);
    triglints.tick(1, seen);
    CHECK(triglints.sprites().empty());
}
