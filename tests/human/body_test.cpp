// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/body.h"

#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "physics/mesh_sweep.h"
#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;

// The walls themselves (the 0.25 m rules, the sweep, the corner slide, the airborne push-out) are the physics world's:
// tests/physics/mesh_sweep_test.cpp and body_sweep_test.cpp.

TEST_CASE("the walking sphere is 0.35 m times the scale, its bottom 0.05 m above the feet", "[human][body]") {
    CHECK(coney::human::walkingRadius(1.0F) == Approx(0.35F));
    CHECK(coney::human::walkingRadius(0.97F) == Approx(0.3395F));
    CHECK(coney::human::walkingCentreHeight(0.97F) == Approx(0.3895F));
}

TEST_CASE("a player's walking sphere counts the scale twice, an AI's body factor is 1", "[human][body]") {
    CHECK(coney::human::playerWalkingRadius(0.97F) == Approx(0.4704F).margin(0.0002));
    CHECK(coney::human::aiWalkingRadius(1.01F) == Approx(0.357F).margin(0.0002));
}

TEST_CASE("wallClassOf classes a triangle as the walking sweep's wall test does", "[human][body]") {
    using coney::human::WallClass;
    // Two floor triangles, a 2 m wall facing -x, a 0.2 m kerb riser facing -y and a ceiling (a floor turned over).
    std::vector<coney::test::Tri> tris = coney::test::floorAt(0.0F, 0.0F, 20.0F, 0.0F, 20.0F);
    tris = coney::test::join(tris, coney::test::wallFacingMinusX(10.0F, 2.0F, 6.0F, 0.0F, 2.0F));
    tris = coney::test::join(tris, coney::test::wallFacingMinusY(12.0F, 2.0F, 6.0F, 0.0F, 0.2F));
    tris.push_back(coney::test::Tri{{2, 2, 3}, {3, 3, 3}, {3, 2, 3}});
    const auto mesh = coney::test::makeMesh(tris);
    CHECK(coney::human::wallClassOf(*mesh, 0) == WallClass::Floor);
    CHECK(coney::human::wallClassOf(*mesh, 1) == WallClass::Floor);
    CHECK(coney::human::wallClassOf(*mesh, 2) == WallClass::Wall);
    CHECK(coney::human::wallClassOf(*mesh, 3) == WallClass::Wall);
    CHECK(coney::human::wallClassOf(*mesh, 4) == WallClass::LowWall);
    CHECK(coney::human::wallClassOf(*mesh, 5) == WallClass::LowWall);
    CHECK(coney::human::wallClassOf(*mesh, 6) == WallClass::Ceiling);
    // The physics sweep's own steepness rule: |n.z| at most 0.65, exactly 0.65 still a wall.
    CHECK(coney::physics::capsuleWallNormal(Vec3{1.0F, 0.0F, 0.0F}));
    CHECK(coney::physics::capsuleWallNormal(Vec3{0.76F, 0.0F, 0.65F}));
    CHECK_FALSE(coney::physics::capsuleWallNormal(Vec3{0.0F, 0.6F, 0.8F}));
}

TEST_CASE("wallClassOf classes a zero-area triangle as degenerate and a disabled one as disabled", "[human][body]") {
    using coney::human::WallClass;
    std::vector<coney::test::Tri> tris{coney::test::Tri{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}};
    tris = coney::test::join(tris, coney::test::wallFacingMinusX(10.0F, 2.0F, 6.0F, 0.0F, 2.0F));
    auto mesh = coney::test::makeMesh(tris);
    CHECK(coney::human::wallClassOf(*mesh, 0) == WallClass::Degenerate);
    // The wall switched off, as a door's are.
    mesh->setEnabledInBox(coney::raycast::Vec3{9.9F, 1.9F, -0.1F}, coney::raycast::Vec3{10.1F, 6.1F, 2.1F}, false);
    CHECK(coney::human::wallClassOf(*mesh, 1) == WallClass::Disabled);
}
