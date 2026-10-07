// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/perception.h"

#include <memory>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "raycast/collision_mesh.h"
#include "support/collision_fixtures.h"

// The AI's line of sight through the level's collision (docs/research/ai.md#sight): eye to eye, then eye to chest,
// passing through fences, railings and glass. Synthetic walls only.

using coney::ai::lineOfSight;
using coney::anim::Vec3;
using coney::test::floorAt;
using coney::test::join;
using coney::test::makeMesh;
using coney::test::wallFacingMinusY;

namespace {

// The two humans' feet: 6 m apart along +y, a wall (if any) at y = 43.
constexpr Vec3 kFrom{40.0F, 40.0F, 0.0F};
constexpr Vec3 kTo{40.0F, 46.0F, 0.0F};

} // namespace

TEST_CASE("with no collision every line of sight is clear", "[ai][sight]") {
    CHECK(lineOfSight(nullptr, kFrom, kTo).clear);
}

TEST_CASE("a wall between two humans blocks the sight; open ground does not", "[ai][sight]") {
    const auto open = makeMesh(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    CHECK(lineOfSight(open.get(), kFrom, kTo).clear);

    const auto walled = makeMesh(
        join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), wallFacingMinusY(43.0F, 30.0F, 50.0F, 0.0F, 4.0F, 0, 5)));
    const coney::ai::SightLine line = lineOfSight(walled.get(), kFrom, kTo);
    CHECK_FALSE(line.clear);
    CHECK(line.crossedMaterial == 5);
}

TEST_CASE("the second ray to the chest sees under what blocks the eyes", "[ai][sight]") {
    // A beam from 1.4 m to 2.5 m: the eye ray (1.7 m) hits it, the eye-to-chest ray (1.7 m down to 1.0 m) passes under
    // it at y = 43 (1.35 m there).
    const auto beam = makeMesh(
        join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), wallFacingMinusY(43.0F, 30.0F, 50.0F, 1.4F, 2.5F, 0, 5)));
    const coney::ai::SightLine line = lineOfSight(beam.get(), kFrom, kTo);
    CHECK(line.clear);
    CHECK(line.crossedMaterial == 5);
}

TEST_CASE("an AI sees through fences, railings and glass", "[ai][sight]") {
    for (const std::uint8_t material : coney::ai::kSightSeeThrough) {
        const auto mesh = makeMesh(join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                        wallFacingMinusY(43.0F, 30.0F, 50.0F, 0.0F, 4.0F, 0, material)));
        CHECK(lineOfSight(mesh.get(), kFrom, kTo).clear);
    }
}
