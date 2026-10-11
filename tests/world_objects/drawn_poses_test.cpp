// SPDX-License-Identifier: GPL-3.0-or-later
// The world objects' poses drawn between the last two steps (docs/guides/conventions.md#update-and-render), on
// synthetic poses.
#include "world_objects/drawn_poses.h"

#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::Quat;
using coney::anim::Vec3;
using coney::world_objects::blendPose;
using coney::world_objects::DrawnPoses;
using coney::world_objects::WorldPose;

namespace {

// A turn of `angle` radians about z.
Quat turn(float angle) { return Quat{0.0F, 0.0F, std::sin(angle / 2.0F), std::cos(angle / 2.0F)}; }

// Whether two poses are the same bit for bit.
bool same(const WorldPose& a, const WorldPose& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.rotation.w == b.rotation.w;
}

const WorldPose kFrom{.position = Vec3{0.0F, 0.0F, 1.0F}, .rotation = turn(0.0F)};
const WorldPose kTo{.position = Vec3{2.0F, -4.0F, 3.0F}, .rotation = turn(std::numbers::pi_v<float> / 2.0F)};

} // namespace

TEST_CASE("a blended pose is the step before at alpha 0, halfway at 0.5 and the newest bit for bit at 1",
          "[drawn_poses]") {
    CHECK(same(blendPose(kFrom, kTo, 0.0F), kFrom));
    CHECK(same(blendPose(kFrom, kTo, 1.0F), kTo));
    const WorldPose half = blendPose(kFrom, kTo, 0.5F);
    CHECK(half.position.x == Approx(1.0F));
    CHECK(half.position.y == Approx(-2.0F));
    CHECK(half.position.z == Approx(2.0F));
    // The rotation as a rotation: a quarter turn halved is an eighth.
    const Quat eighth = turn(std::numbers::pi_v<float> / 4.0F);
    CHECK(half.rotation.z == Approx(eighth.z));
    CHECK(half.rotation.w == Approx(eighth.w));
}

TEST_CASE("an object drawn on two steps is drawn between them", "[drawn_poses]") {
    DrawnPoses<double> poses;
    poses.commit();
    poses.record(7.0, kFrom);
    poses.commit();
    poses.record(7.0, kTo);
    CHECK(same(poses.drawn(7.0, kTo, 0.0F), kFrom));
    CHECK(poses.drawn(7.0, kTo, 0.5F).position.x == Approx(1.0F));
    CHECK(same(poses.drawn(7.0, kTo, 1.0F), kTo));
}

TEST_CASE("a just-spawned object is drawn where it is, with nothing to blend from", "[drawn_poses]") {
    DrawnPoses<double> poses;
    poses.commit();
    poses.record(1.0, kFrom);
    poses.commit();
    poses.record(1.0, kFrom);
    poses.record(2.0, kTo);
    CHECK(same(poses.drawn(2.0, kTo, 0.0F), kTo));
    CHECK(same(poses.drawn(2.0, kTo, 0.5F), kTo));
    // An object not recorded this step (one in a hand) is drawn as given too.
    CHECK(same(poses.drawn(3.0, kTo, 0.25F), kTo));
}

TEST_CASE("an object that left the world and came back is not blended from before it left", "[drawn_poses]") {
    DrawnPoses<double> poses;
    poses.commit();
    poses.record(4.0, kFrom);
    poses.commit();
    poses.commit();
    poses.record(4.0, kTo);
    CHECK(same(poses.drawn(4.0, kTo, 0.5F), kTo));
}

TEST_CASE("a jump (a new placement) is not blended", "[drawn_poses]") {
    DrawnPoses<double> poses;
    poses.commit();
    poses.record(5.0, kFrom, 0);
    poses.commit();
    poses.record(5.0, kTo, 1);
    CHECK(same(poses.drawn(5.0, kTo, 0.5F), kTo));
    // Nor is a move farther in one step than anything in the game travels.
    const WorldPose far{.position = Vec3{DrawnPoses<double>::kMostBlendedMetres + 1.0F, 0.0F, 1.0F},
                        .rotation = kFrom.rotation};
    poses.commit();
    poses.record(5.0, kFrom, 1);
    poses.commit();
    poses.record(5.0, far, 1);
    CHECK(same(poses.drawn(5.0, far, 0.5F), far));
}
