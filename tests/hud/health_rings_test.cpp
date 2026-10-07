// SPDX-License-Identifier: GPL-3.0-or-later
// The health rings (hud/health_rings.h): their arcs and colours, the low-health blink, when a player's rings show,
// hold and fade, the target's fade-in and L1 marker, the hit pulse and the HUD hiding them. Synthetic humans.
#include "hud/health_rings.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::hud::GroundRing;
using coney::hud::HealthRings;
using coney::hud::RingArc;
using coney::hud::RingColour;
using coney::hud::RingFrame;
using coney::hud::RingHuman;
using coney::hud::RingPlayer;

namespace {

// Player 1 (id 1) at the origin with `health` percent, and nothing pressed.
RingFrame frameAt(std::uint64_t nowMs, float health = 90.0F) {
    RingFrame frame;
    frame.nowMs = nowMs;
    RingPlayer player;
    player.human = RingHuman{.id = 1, .healthPercent = health, .player = true};
    frame.players.push_back(player);
    return frame;
}

// Steps `rings` at 1/30 s from `fromMs` to `toMs` with `frame`'s humans, SELECT and the rest released.
void run(HealthRings& rings, std::uint64_t fromMs, std::uint64_t toMs, RingFrame frame) {
    for (std::uint64_t t = fromMs; t <= toMs; t += 33) {
        frame.nowMs = t;
        rings.update(frame);
    }
}

} // namespace

TEST_CASE("the health colour goes green, orange to red, near black", "[health_rings]") {
    CHECK(coney::hud::healthColour(90.0F) == RingColour{76, 122, 27});
    CHECK(coney::hud::healthColour(50.0F) == RingColour{158, 81, 24});
    CHECK(coney::hud::healthColour(0.05F) == RingColour{16, 16, 16});
}

TEST_CASE("a ring's rim splits into fill, lost and beyond-capacity arcs", "[health_rings]") {
    // Rembrandt at 35 % health: value 35 × 0.35 = 12.25, capacity 35.
    CHECK(coney::hud::ringArcOf(0, 12.25F, 35.0F) == RingArc::Fill);
    CHECK(coney::hud::ringArcOf(7, 12.25F, 35.0F) == RingArc::Fill);
    CHECK(coney::hud::ringArcOf(9, 12.25F, 35.0F) == RingArc::Lost);
    CHECK(coney::hud::ringArcOf(22, 12.25F, 35.0F) == RingArc::Lost);
    CHECK(coney::hud::ringArcOf(23, 12.25F, 35.0F) == RingArc::Rest);
    CHECK(coney::hud::ringArcOf(64, 12.25F, 35.0F) == RingArc::Rest);
    // Full: no rest arc, and only the closing vertex 64 lost (the page's rule as written).
    CHECK(coney::hud::ringArcOf(63, 100.0F, 100.0F) == RingArc::Fill);
    CHECK(coney::hud::ringArcOf(64, 100.0F, 100.0F) == RingArc::Lost);
}

TEST_CASE("a ring is 64 triangles in one colour each, round its centre", "[health_rings]") {
    GroundRing ring;
    ring.radius = 0.54F;
    ring.value = 50.0F;
    ring.capacity = 100.0F;
    ring.fill = RingColour{1, 2, 3};
    ring.alpha = 200;
    const std::vector<coney::hud::RingVertex> fan = coney::hud::ringFan(ring);
    REQUIRE(fan.size() == 192);
    for (std::size_t t = 0; t < fan.size(); t += 3) {
        CHECK(fan[t].rgba == fan[t + 2].rgba);
        CHECK(fan[t + 1].rgba == fan[t + 2].rgba);
        CHECK(fan[t].rgba[3] == 200);
    }
    CHECK(fan[0].rgba[0] == 1);                // the first segment: the fill
    CHECK(fan.back().rgba[0] == 0);            // the last: lost (black)
    CHECK(fan[1].position.x == Approx(0.54F)); // start angle 0: the first rim vertex on +x
    CHECK(fan[1].du == Approx(0.062F));
}

TEST_CASE("SELECT shows the player's rings: fade in, hold 4 s, fade out", "[health_rings]") {
    HealthRings rings;
    run(rings, 0, 1000, frameAt(0));
    CHECK(rings.rings().empty()); // no trigger at 90 %: nothing
    RingFrame press = frameAt(1033);
    press.players[0].selectPressed = true;
    rings.update(press);
    run(rings, 1066, 1700, frameAt(0));
    REQUIRE(rings.rings().size() == 2); // outer and inner
    CHECK(rings.rings()[0].alpha == 255);
    CHECK(rings.rings()[0].radius == Approx(coney::hud::kOuterRingRadius));
    CHECK(rings.rings()[1].radius == Approx(coney::hud::kInnerRingRadius));
    CHECK(rings.rings()[0].centre.z == Approx(coney::hud::kRingHeight));
    // Health 90 % of Rembrandt's 35: the fill to 31.5 %, capacity 35 %.
    CHECK(rings.rings()[0].value == Approx(31.5F));
    CHECK(rings.rings()[0].capacity == Approx(35.0F));
    run(rings, 1733, 5033 + 250, frameAt(0));
    REQUIRE_FALSE(rings.rings().empty());
    CHECK(rings.rings()[0].alpha < 200); // fading out after 4 s
    run(rings, 5300, 5700, frameAt(0));
    CHECK(rings.rings().empty());
}

TEST_CASE("a fight stance, low health or the force flag keep the rings up", "[health_rings]") {
    HealthRings rings;
    run(rings, 0, 6000, frameAt(0, 15.0F));
    CHECK(rings.rings().size() == 2);
    HealthRings forced;
    RingFrame frame = frameAt(0);
    frame.forceAll = true;
    forced.update(frame);
    REQUIRE(forced.rings().size() == 2);
    CHECK(forced.rings()[0].alpha == 255);
}

TEST_CASE("low health blinks the fill black every 232 ms", "[health_rings]") {
    HealthRings rings;
    RingFrame frame = frameAt(0, 20.0F);
    frame.forceAll = true;
    frame.nowMs = 100;
    rings.update(frame);
    CHECK(rings.rings()[0].fill == coney::hud::kRingLost);
    frame.nowMs = 300;
    rings.update(frame);
    CHECK(rings.rings()[0].fill == coney::hud::healthColour(20.0F));
}

TEST_CASE("nothing while the HUD is hidden", "[health_rings]") {
    HealthRings rings;
    RingFrame frame = frameAt(0, 10.0F);
    frame.forceAll = true;
    frame.hudShown = false;
    rings.update(frame);
    CHECK(rings.rings().empty());
}

TEST_CASE("the target's outer ring fades in over 500 ms; L1 lays the marker under it", "[health_rings]") {
    HealthRings rings;
    RingFrame frame = frameAt(0);
    frame.players[0].target = RingHuman{.id = 7, .healthPercent = 50.0F};
    frame.players[0].holdingL1 = true;
    run(rings, 0, 264, frame);
    REQUIRE(rings.rings().size() == 1); // the target's outer ring only
    CHECK(rings.rings()[0].id == 7);
    CHECK(rings.rings()[0].alpha == static_cast<std::uint8_t>(264 * 0.51F));
    REQUIRE(rings.markers().size() == 1);
    CHECK(rings.markers()[0].alpha == rings.rings()[0].alpha);
    run(rings, 297, 900, frame);
    CHECK(rings.rings()[0].alpha == 255);
}

TEST_CASE("a hit swells the rings by up to 15 % and they shrink back", "[health_rings]") {
    HealthRings rings;
    RingFrame frame = frameAt(0);
    frame.forceAll = true;
    frame.players[0].human.damageTaken = 64;
    rings.update(frame);
    CHECK(rings.rings()[0].radius == Approx(coney::hud::kOuterRingRadius * 1.15F));
    frame.players[0].human.damageTaken = 0;
    for (int i = 0; i < 4; ++i) {
        rings.update(frame);
    }
    CHECK(rings.rings()[0].radius == Approx(coney::hud::kOuterRingRadius));
}

TEST_CASE("rage full flashes the outer ring gold three times", "[health_rings]") {
    HealthRings rings;
    RingFrame frame = frameAt(0);
    frame.forceAll = true;
    frame.players[0].human.rageFull = true;
    rings.update(frame);
    CHECK(rings.rings()[0].fill == coney::hud::kRingRageFlash);
    CHECK(rings.rings()[0].rest == coney::hud::kRingRageFlash);
    frame.nowMs = 300;
    rings.update(frame);
    CHECK(rings.rings()[0].fill == coney::hud::healthColour(90.0F));
    frame.nowMs = 1300;
    rings.update(frame);
    CHECK(rings.rings()[0].fill == coney::hud::healthColour(90.0F));
}
