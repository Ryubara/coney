// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/water.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using coney::effects::Water;
using coney::effects::WaterSettings;

namespace {

// A level84-like surface: 1 m waves, speed 0.25.
WaterSettings settings() {
    WaterSettings s;
    s.position = {10.0F, 20.0F, -1.0F};
    s.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
    s.width = 200.0F;
    s.length = 400.0F;
    s.colour = {40, 60, 80};
    s.waveHeight = 1.0F;
    s.waveSpeed = 0.25F;
    return s;
}

} // namespace

TEST_CASE("the water is a 3 x 9 grid placed by its corner and scaled to its size", "[effects][water]") {
    Water water;
    CHECK_FALSE(water.placed());
    water.set(settings());
    REQUIRE(water.placed());
    CHECK(water.vertices().size() == 27);
    CHECK(Water::indices().size() == 2 * 8 * 6);
    const auto far = water.toWorld({1.0F, 1.0F, 0.0F});
    CHECK(far.x == Catch::Approx(210.0F));
    CHECK(far.y == Catch::Approx(420.0F));
    CHECK(far.z == Catch::Approx(-1.0F));
}

TEST_CASE("each column waves by sin(i + t), its alpha 225 + 5 sin, and the last copies the first", "[effects][water]") {
    Water water;
    water.set(settings());
    // Phase 0: column i at sin(i).
    const auto& v = water.vertices();
    CHECK(v[1].position.z == Catch::Approx(std::sin(1.0F)));
    CHECK(v[1].colour[3] == static_cast<std::uint8_t>(225.0F + 5.0F * std::sin(1.0F)));
    CHECK(v[1].colour[0] == 40);
    CHECK(v[2].position.z == Catch::Approx(v[0].position.z));
    CHECK(v[1 + 3 * 5].position.z == Catch::Approx(v[1].position.z)); // the same down the column
    CHECK(v[2].u == Catch::Approx(4.0F));
    CHECK(v[3 * 8].v == Catch::Approx(16.0F));
}

TEST_CASE("the grid updates every second frame, scrolling the texture", "[effects][water]") {
    Water water;
    water.set(settings());
    const float before = water.vertices()[1].position.z;
    water.step(); // phase 0.16: not past 0.3 yet
    CHECK(water.vertices()[1].position.z == Catch::Approx(before));
    water.step(); // phase 0.32: updated with t = 0.08
    CHECK(water.vertices()[1].position.z == Catch::Approx(std::sin(1.0F + 0.08F)));
    CHECK(water.vertices()[0].u == Catch::Approx(0.08F * 0.3F));
}
