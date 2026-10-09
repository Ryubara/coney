// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "human/strike_shapes.h"

// The strike shapes' switching by clip events, their posing on the bones and their overlap tests
// (docs/research/combat.md#moving-strikes). Synthetic bones only.

using Catch::Approx;
using coney::anim::ClipEvent;
using coney::anim::Mat34;
using coney::anim::Vec3;
namespace human = coney::human;

namespace {

// A clip event of `type` with `word` in its +6 word.
ClipEvent event(std::uint16_t type, std::uint16_t word = 0) {
    ClipEvent e;
    e.type = type;
    e.word = word;
    return e;
}

// Every bone at `at`, unrotated.
std::array<Mat34, coney::anim::kPoseBones> bonesAt(Vec3 at) {
    std::array<Mat34, coney::anim::kPoseBones> bones{};
    for (Mat34& bone : bones) {
        bone.t = at;
    }
    return bones;
}

} // namespace

TEST_CASE("the ten bone shapes: spine and head are the targets", "[human][strike]") {
    const auto defs = human::strikeShapeDefs();
    REQUIRE(defs.size() == 10);
    int targets = 0;
    for (const human::StrikeShapeDef& def : defs) {
        targets += def.target ? 1 : 0;
    }
    CHECK(targets == 2);
    CHECK(defs[0].bone == 3);
    CHECK(defs[0].segment);
    CHECK(defs[0].radius == Approx(0.18F));
    CHECK(defs[1].bone == 6);
    CHECK_FALSE(defs[1].segment);
}

TEST_CASE("clip events switch one shape, or all with the capsule, and turning the last off forgets the struck",
          "[human][strike]") {
    human::StrikeShapes shapes;
    shapes.onEvent(event(human::kEventStrikeOn, 25));
    CHECK(shapes.on(25));
    CHECK_FALSE(shapes.on(24));
    CHECK_FALSE(shapes.capsuleOn());
    shapes.markStruck(7.0);
    CHECK(shapes.struck(7.0));
    // An id without a shape sets its bit and switches nothing posed.
    shapes.onEvent(event(human::kEventStrikeOn, 23));
    CHECK(shapes.active().size() == 1);
    shapes.onEvent(event(human::kEventStrikeOff, 25));
    shapes.onEvent(event(human::kEventStrikeOff, 23));
    CHECK_FALSE(shapes.anyOn());
    CHECK_FALSE(shapes.struck(7.0));

    shapes.onEvent(event(human::kEventStrikeAllOn));
    CHECK(shapes.active().size() == 10);
    CHECK(shapes.capsuleOn());
    int human = 0;
    shapes.markStruckHuman(&human);
    CHECK(shapes.struckHuman(&human));
    shapes.onEvent(event(human::kEventStrikeAllOff));
    CHECK_FALSE(shapes.anyOn());
    CHECK_FALSE(shapes.capsuleOn());
    CHECK_FALSE(shapes.struckHuman(&human));
}

TEST_CASE("a segment runs along its bone's x from the offset, turned and moved into the world", "[human][strike]") {
    const human::StrikeShapeDef spine = human::strikeShapeDefs()[0];
    // The bones 1 m above the feet, the body turned a quarter left: character +x is world +y.
    const auto posed = human::poseStrikeShapes(
        std::array{spine}, bonesAt(Vec3{0.0F, 0.0F, 1.0F}),
        human::BodyPlacement{.feet = {10.0F, 0.0F, 0.0F}, .heading = std::numbers::pi_v<float> / 2.0F});
    REQUIRE(posed.size() == 1);
    CHECK(posed[0].a.x == Approx(10.0F).margin(1e-5));
    CHECK(posed[0].a.y == Approx(-0.07F).margin(1e-5));
    CHECK(posed[0].a.z == Approx(1.05F).margin(1e-5));
    CHECK(posed[0].b.y == Approx(-0.07F + 0.38F).margin(1e-5));
    CHECK(posed[0].radius == Approx(0.18F));
}

TEST_CASE("the body scale scales offsets, lengths and radii", "[human][strike]") {
    const human::StrikeShapeDef spine = human::strikeShapeDefs()[0];
    const auto posed =
        human::poseStrikeShapes(std::array{spine}, bonesAt(Vec3{}), human::BodyPlacement{.feet = {}, .scale = 2.0F});
    REQUIRE(posed.size() == 1);
    CHECK(posed[0].a.x == Approx(-0.14F));
    CHECK(posed[0].b.x == Approx(-0.14F + 0.76F));
    CHECK(posed[0].radius == Approx(0.36F));
}

TEST_CASE("shapes overlap within their radii; segments by their nearest points", "[human][strike]") {
    const human::PosedShape fist{.a = {0.0F, 0.0F, 1.0F}, .b = {0.0F, 0.0F, 1.0F}, .radius = 0.09F};
    const human::PosedShape spine{.a = {0.25F, -0.2F, 1.0F}, .b = {0.25F, 0.2F, 1.0F}, .radius = 0.18F};
    CHECK(human::shapesOverlap(fist, spine));
    const human::PosedShape far{.a = {0.3F, -0.2F, 1.0F}, .b = {0.3F, 0.2F, 1.0F}, .radius = 0.18F};
    CHECK_FALSE(human::shapesOverlap(fist, far));
    // Crossing segments meet whatever their radii.
    const human::PosedShape across{.a = {-1.0F, 0.0F, 1.0F}, .b = {1.0F, 0.0F, 1.0F}, .radius = 0.01F};
    const human::PosedShape upright{.a = {0.0F, 0.0F, 0.0F}, .b = {0.0F, 0.0F, 2.0F}, .radius = 0.01F};
    CHECK(human::shapesOverlap(across, upright));
}

TEST_CASE("a strike is tested along each shape's move since the last update, as X1's hook crosses the spine",
          "[human][strike]") {
    // The target's spine, upright, 1.0 m ahead; a hand sphere crossing in front of its top from right to left
    // (combat-moves.md#reach): neither end of the move overlaps, the path does.
    const human::PosedShape spine{.a = {0.0F, 1.0F, 1.1F}, .b = {0.0F, 1.0F, 1.5F}, .radius = 0.18F, .bone = 3};
    const human::PosedShape from{.a = {0.25F, 0.78F, 1.6F}, .b = {0.25F, 0.78F, 1.6F}, .radius = 0.087F, .bone = 25};
    const human::PosedShape to{.a = {-0.25F, 0.78F, 1.6F}, .b = {-0.25F, 0.78F, 1.6F}, .radius = 0.087F, .bone = 25};
    CHECK_FALSE(human::shapesOverlap(from, spine));
    CHECK_FALSE(human::shapesOverlap(to, spine));
    CHECK(human::sweptShapesMeet(from, to, spine));
    // A shape just switched on has no move: only where it stands counts.
    CHECK_FALSE(human::sweptShapesMeet(to, to, spine));
    // A segment's ends sweep too.
    const human::PosedShape armFrom{.a = {0.6F, 0.78F, 1.6F}, .b = {0.3F, 0.78F, 1.6F}, .radius = 0.087F, .bone = 24};
    const human::PosedShape armTo{.a = {-0.3F, 0.78F, 1.6F}, .b = {-0.6F, 0.78F, 1.6F}, .radius = 0.087F, .bone = 24};
    CHECK(human::sweptShapesMeet(armFrom, armTo, spine));
}

TEST_CASE("a shape touches a triangle it crosses or reaches with its radius", "[human][strike]") {
    // A wall in the plane y = 0, from x 0 to 2 and z 0 to 2.
    const Vec3 p0{0.0F, 0.0F, 0.0F};
    const Vec3 p1{2.0F, 0.0F, 0.0F};
    const Vec3 p2{0.0F, 0.0F, 2.0F};
    const human::PosedShape near{.a = {0.5F, -0.1F, 0.5F}, .b = {0.5F, -0.1F, 0.5F}, .radius = 0.15F};
    CHECK(human::shapeTouchesTriangle(near, p0, p1, p2));
    const human::PosedShape short_{.a = {0.5F, -0.3F, 0.5F}, .b = {0.5F, -0.3F, 0.5F}, .radius = 0.15F};
    CHECK_FALSE(human::shapeTouchesTriangle(short_, p0, p1, p2));
    const human::PosedShape through{.a = {0.5F, -1.0F, 0.5F}, .b = {0.5F, 1.0F, 0.5F}, .radius = 0.01F};
    CHECK(human::shapeTouchesTriangle(through, p0, p1, p2));
    // Past the triangle's hypotenuse, beside the wall, it misses.
    const human::PosedShape beside{.a = {1.8F, -1.0F, 1.8F}, .b = {1.8F, 1.0F, 1.8F}, .radius = 0.05F};
    CHECK_FALSE(human::shapeTouchesTriangle(beside, p0, p1, p2));
}
