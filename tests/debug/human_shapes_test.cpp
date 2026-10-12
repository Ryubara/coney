// SPDX-License-Identifier: GPL-3.0-or-later
// The Debug draw page's collision shapes: the line helpers, a human's bodies drawn from the engine's own sizes, and the
// collision wireframe coloured by the walking sweep's classing. Synthetic humans and meshes: no game, no disc.
#include "debug/human_shapes.h"

#include <cmath>
#include <cstddef>
#include <set>
#include <tuple>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "debug/debug_lines.h"
#include "human/body.h"
#include "human/human.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::debug::DebugLine;
using coney::debug::DebugLines;
using coney::graphics::Rgba;
using coney::human::Human;

namespace {

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// Whether two colours are the same.
bool same(Rgba a, Rgba b) { return std::tie(a.r, a.g, a.b, a.a) == std::tie(b.r, b.g, b.b, b.a); }

// The lines of `lines` in `colour`.
std::vector<DebugLine> linesIn(const DebugLines& lines, Rgba colour) {
    std::vector<DebugLine> found;
    for (const DebugLine& line : lines.lines()) {
        if (same(line.colour, colour)) {
            found.push_back(line);
        }
    }
    return found;
}

// The largest distance from `centre` of any end of `lines`.
float farthest(const std::vector<DebugLine>& lines, Vec3 centre) {
    float most = 0.0F;
    for (const DebugLine& line : lines) {
        most = std::max({most, coney::anim::distance(line.a, centre), coney::anim::distance(line.b, centre)});
    }
    return most;
}

} // namespace

TEST_CASE("debug lines build spheres, capsules, swept spheres and arrows of known sizes", "[debug]") {
    DebugLines lines;
    constexpr Rgba kRed{255, 0, 0, 255};
    lines.sphere(Vec3{1.0F, 2.0F, 3.0F}, 0.5F, kRed);
    // Three great circles of 16 segments, every end on the sphere.
    REQUIRE(lines.size() == std::size_t{3} * DebugLines::kCircleSegments);
    for (const DebugLine& line : lines.lines()) {
        CHECK(coney::anim::distance(line.a, Vec3{1.0F, 2.0F, 3.0F}) == Approx(0.5F).margin(1e-4));
    }
    DebugLines capsule;
    capsule.uprightCapsule(Vec3{}, 2.0F, 0.35F, kRed);
    // A ring at each end and four uprights.
    CHECK(capsule.size() == std::size_t{2} * DebugLines::kCircleSegments + 4U);
    DebugLines swept;
    swept.sweptSphere(Vec3{}, Vec3{1.0F, 0.0F, 0.0F}, 0.1F, kRed);
    CHECK(swept.size() == std::size_t{6} * DebugLines::kCircleSegments + 4U);
    DebugLines point;
    point.sweptSphere(Vec3{}, Vec3{}, 0.1F, kRed);
    CHECK(point.size() == std::size_t{3} * DebugLines::kCircleSegments);
    DebugLines arrow;
    arrow.arrow(Vec3{}, Vec3{0.0F, 0.0F, 1.0F}, kRed);
    REQUIRE_FALSE(arrow.empty());
    CHECK(arrow.lines().front().b.z == Approx(1.0F));
    lines.append(arrow);
    CHECK(lines.size() == std::size_t{3} * DebugLines::kCircleSegments + arrow.size());
}

TEST_CASE("a human's walking sphere is drawn at the radius and height the wall sweep uses", "[debug][human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    for (const bool player : {true, false}) {
        INFO(player);
        Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
        human.setFighterProfile(coney::human::FighterProfile{.player = player});
        human.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        // The sweep's own radius: the player's 0.4704 m, an AI human's 0.35 m (docs/research/characters.md#walls).
        CHECK(human.walkingSphereRadius() ==
              Approx(player ? coney::human::playerWalkingRadius(1.0F) : coney::human::aiWalkingRadius(1.0F)));
        CHECK(human.walkingSphereCentreHeight() ==
              Approx(human.walkingSphereRadius() + coney::human::bodyTuning().footGap));
        DebugLines lines;
        coney::debug::addHumanShapes(lines, human, coney::debug::ShapeLayers{});
        const std::vector<DebugLine> sphere = linesIn(lines, coney::debug::kWalkingSphereColour);
        REQUIRE(sphere.size() == std::size_t{3} * DebugLines::kCircleSegments);
        const Vec3 centre = coney::anim::add(human.position(), Vec3{0.0F, 0.0F, human.walkingSphereCentreHeight()});
        CHECK(farthest(sphere, centre) == Approx(human.walkingSphereRadius()).margin(1e-4));
        // The capsule others meet, and the ground probe under a grounded human.
        const coney::human::UprightCapsule capsule = human.uprightCapsule();
        const std::vector<DebugLine> uprights = linesIn(lines, coney::debug::kCapsuleColour);
        REQUIRE_FALSE(uprights.empty());
        float top = 0.0F;
        for (const DebugLine& line : uprights) {
            top = std::max({top, line.a.z, line.b.z});
        }
        CHECK(top == Approx(capsule.feet.z + capsule.top));
        CHECK(linesIn(lines, coney::debug::kGroundProbeColour).size() == 1U);
        // Nothing of the layers switched off.
        CHECK(linesIn(lines, coney::debug::kStrikeColour).empty());
        CHECK(linesIn(lines, coney::debug::kContactColour).empty());
    }
}

TEST_CASE("a contact is drawn where the walking sphere touches what it slid along", "[debug][human]") {
    const Vec3 centre{0.0F, 0.0F, 0.5F};
    // A wall facing -x touched by a sphere on its -x side: the point is on the sphere, towards +x.
    const Vec3 point = coney::debug::contactPoint(centre, 0.47F, Vec3{-1.0F, 0.0F, 0.0F});
    CHECK(point.x == Approx(0.47F));
    CHECK(point.y == Approx(0.0F));
    CHECK(point.z == Approx(0.5F));
    // A normal of no length leaves the centre.
    CHECK(coney::debug::contactPoint(centre, 0.47F, Vec3{}).z == Approx(0.5F));
}

TEST_CASE("the classified wireframe colours each triangle by the walking sweep's classing", "[debug][human]") {
    using coney::human::WallClass;
    // Every class has its own colour.
    std::set<std::tuple<int, int, int>> colours;
    for (const WallClass kind : {WallClass::Disabled, WallClass::Degenerate, WallClass::Floor, WallClass::Ceiling,
                                 WallClass::LowWall, WallClass::Wall}) {
        const Rgba c = coney::debug::wallClassColour(kind);
        colours.emplace(c.r, c.g, c.b);
    }
    CHECK(colours.size() == 6U);
    // A floor, a wall and a kerb riser near the centre, and a wall far away.
    std::vector<coney::test::Tri> tris = coney::test::floorAt(0.0F, 0.0F, 4.0F, 0.0F, 4.0F);
    tris = coney::test::join(tris, coney::test::wallFacingMinusX(3.0F, 0.0F, 3.0F, 0.0F, 2.0F));
    tris = coney::test::join(tris, coney::test::wallFacingMinusY(3.5F, 0.0F, 3.0F, 0.0F, 0.2F));
    tris = coney::test::join(tris, coney::test::wallFacingMinusX(70.0F, 70.0F, 73.0F, 0.0F, 2.0F));
    const auto mesh = coney::test::makeMesh(tris);
    DebugLines lines;
    const std::size_t drawn =
        coney::debug::addCollisionWireframe(lines, *mesh, Vec3{1.0F, 1.0F, 0.0F}, 10.0F, true, 100);
    CHECK(drawn == 6U);
    CHECK(lines.size() == 18U);
    CHECK(linesIn(lines, coney::debug::wallClassColour(WallClass::Floor)).size() == 6U);
    CHECK(linesIn(lines, coney::debug::wallClassColour(WallClass::Wall)).size() == 6U);
    CHECK(linesIn(lines, coney::debug::wallClassColour(WallClass::LowWall)).size() == 6U);
    // Unclassified, one colour; and the budget is kept.
    DebugLines plain;
    CHECK(coney::debug::addCollisionWireframe(plain, *mesh, Vec3{1.0F, 1.0F, 0.0F}, 10.0F, false, 4) == 4U);
    CHECK(linesIn(plain, coney::debug::wallClassColour(WallClass::Wall)).empty());
}

TEST_CASE("the inspector's highlight stands over the selection", "[debug]") {
    DebugLines lines;
    coney::debug::addHighlight(lines, Vec3{5.0F, 5.0F, 1.0F}, 2.4F);
    REQUIRE_FALSE(lines.empty());
    float top = 0.0F;
    for (const DebugLine& line : lines.lines()) {
        CHECK(same(line.colour, coney::debug::kHighlightColour));
        top = std::max({top, line.a.z, line.b.z});
    }
    CHECK(top >= 3.4F);
}

TEST_CASE("a human's shapes drawn at an offset are the same lines moved by it", "[debug][human]") {
    // The shapes are drawn where the models are, which is between steps: the play mode gives the offset from the
    // newest step's feet to the drawn ones (docs/guides/debug-menu.md#debug-draw).
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const coney::debug::ShapeLayers layers{.bodies = true, .strikes = true, .contacts = true};
    DebugLines here;
    coney::debug::addHumanShapes(here, human, layers);
    DebugLines moved;
    const Vec3 offset{0.25F, -0.5F, 0.125F};
    coney::debug::addHumanShapes(moved, human, layers, offset);
    REQUIRE(here.size() > 0);
    REQUIRE(moved.size() == here.size());
    for (std::size_t i = 0; i < here.size(); ++i) {
        CHECK(moved.lines()[i].a.x == Approx(here.lines()[i].a.x + offset.x));
        CHECK(moved.lines()[i].a.y == Approx(here.lines()[i].a.y + offset.y));
        CHECK(moved.lines()[i].b.z == Approx(here.lines()[i].b.z + offset.z));
        CHECK(same(moved.lines()[i].colour, here.lines()[i].colour));
    }
}

TEST_CASE("appending debug lines at an offset moves every end", "[debug]") {
    DebugLines source;
    constexpr Rgba kRed{255, 0, 0, 255};
    source.add(Vec3{0.0F, 0.0F, 0.0F}, Vec3{1.0F, 0.0F, 0.0F}, kRed);
    DebugLines target;
    target.appendOffset(source, Vec3{0.0F, 2.0F, 3.0F});
    REQUIRE(target.size() == 1);
    CHECK(target.lines()[0].a.y == Approx(2.0F));
    CHECK(target.lines()[0].b.x == Approx(1.0F));
    CHECK(target.lines()[0].b.z == Approx(3.0F));
    CHECK(same(target.lines()[0].colour, kRed));
}
