// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/follow_collision.h"

#include <memory>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0F;
// Straight along -y, the way the camera's ray goes behind a player facing +y.
constexpr Vec3 kBack{0.0F, -1.0F, 0.0F};

// A two-sided panel across y = `y` (x 30-50, z 0-3), switched off as the game switches off doors and glass.
std::unique_ptr<coney::raycast::CollisionMesh> disabledPanel(float y) {
    auto mesh = coney::test::makeMesh(
        coney::test::wallFacingPlusY(y, 30.0F, 50.0F, 0.0F, 3.0F, coney::raycast::kTriangleTwoSided));
    mesh->setEnabledInBox(coney::raycast::Vec3{29.0F, y - 0.1F, -1.0F}, coney::raycast::Vec3{51.0F, y + 0.1F, 4.0F},
                          false);
    return mesh;
}

} // namespace

TEST_CASE("the probe angle is 7 degrees at the band's near edge and 4 at its far edge", "[camera]") {
    CHECK(coney::camera::probeAngle(4.8F, 4.8F, 5.3F) == Approx(7.0F * kDegree));
    CHECK(coney::camera::probeAngle(5.05F, 4.8F, 5.3F) == Approx(5.5F * kDegree));
    CHECK(coney::camera::probeAngle(5.3F, 4.8F, 5.3F) == Approx(4.0F * kDegree));
    CHECK(coney::camera::probeAngle(2.0F, 4.8F, 5.3F) == Approx(7.0F * kDegree));
    CHECK(coney::camera::probeAngle(9.0F, 4.8F, 5.3F) == Approx(4.0F * kDegree));
}

TEST_CASE("the view ray ignores a disabled triangle near the look-at point or with the target behind it", "[camera]") {
    const Vec3 lookAt{40.0F, 40.0F, 1.4F};
    const Vec3 feet{40.0F, 40.0F, 0.0F};
    // 0.3 m in front of the panel: cast again without disabled triangles, so nothing is hit.
    CHECK_FALSE(coney::camera::castViewRay(*disabledPanel(39.7F), lookAt, kBack, 4.8F, feet));
    // 2 m in front of it, with the target in front too: the panel stands.
    const auto far = coney::camera::castViewRay(*disabledPanel(38.0F), lookAt, kBack, 4.8F, feet);
    REQUIRE(far);
    // value_or keeps the access checked for clang-tidy, which does not know REQUIRE stops the test.
    CHECK(far.value_or(coney::raycast::RayHit{}).t == Approx(2.0F));
    // 2 m in front of it, but the target's point behind it: cast again, nothing.
    CHECK_FALSE(coney::camera::castViewRay(*disabledPanel(38.0F), lookAt, kBack, 4.8F, Vec3{40.0F, 37.0F, 0.0F}));
    // An enabled triangle always stands, however near.
    const auto wall = coney::test::makeMesh(coney::test::wallFacingPlusY(39.7F, 30.0F, 50.0F, 0.0F, 3.0F));
    const auto near = coney::camera::castViewRay(*wall, lookAt, kBack, 4.8F, feet);
    REQUIRE(near);
    CHECK(near.value_or(coney::raycast::RayHit{}).t == Approx(0.3F));
}

TEST_CASE("the side room counts the clear probes out to 3 probe angles on each side", "[camera]") {
    const Vec3 lookAt{40.0F, 40.0F, 1.4F};
    const Vec3 feet{40.0F, 40.0F, 0.0F};
    const float angle = 7.0F * kDegree;
    // In the open: 21° of room both ways.
    const auto open = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    const auto free = coney::camera::sideRoom(*open, lookAt, kBack, 4.8F, angle, feet);
    CHECK(free[0] == Approx(21.0F * kDegree));
    CHECK(free[1] == Approx(21.0F * kDegree));
    // A wall 0.9 m to +x, beside the ray's far end: the probe 2 × 7° anticlockwise (toward +x) meets it.
    const auto side = coney::test::makeMesh(coney::test::wallFacingMinusX(40.9F, 30.0F, 37.0F, -5.0F, 10.0F));
    const auto room = coney::camera::sideRoom(*side, lookAt, kBack, 4.8F, angle, feet);
    CHECK(room[0] == Approx(7.0F * kDegree));
    CHECK(room[1] == Approx(21.0F * kDegree));
}
