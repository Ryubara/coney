// SPDX-License-Identifier: GPL-3.0-or-later
// The breakable glass panes (docs/research/objects.md#pane, docs/research/objects.md#shatter): the spawn's geometry
// and triangles, the first hit breaking a pane, the shatter's count and sound, the alarm and window link, the
// pre-broken types and BreakGlassInRadius.
#include "world_objects/glass.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "raycast/collision_mesh.h"
#include "support/object_fixtures.h"
#include "world/path_map.h"

using coney::world_objects::GlassPane;
using coney::world_objects::GlassPanes;
using coney::world_objects::GlassSpawn;
using coney::world_objects::GlassType;
using coney::world_objects::shatterPlan;
namespace material = coney::world_objects::material;

namespace {

// A 2 m wide, 2 m high pane of `type` standing in the plane x = 4 from (4, y0, 0), on triangles 2 and 3.
GlassSpawn pane(int type, float y0 = 11.0F) {
    return GlassSpawn{.type = type,
                      .corner = {4.0F, y0, 0.0F},
                      .cornerU = {4.0F, y0 + 2.0F, 0.0F},
                      .cornerV = {4.0F, y0, 2.0F},
                      .triangles = {2, 3}};
}

} // namespace

TEST_CASE("the shatter: 10 shards per square metre, small panes and the cap", "[world_objects][glass]") {
    const auto size = [](std::uint32_t w, std::uint32_t h) { return w | (h << 16U); };
    CHECK(shatterPlan(size(2, 2)).count == 40);
    CHECK(shatterPlan(size(2, 2)).soundMaterial == material::kGlass);
    CHECK(shatterPlan(size(3, 3)).count == 79); // 90, capped
    CHECK(shatterPlan(size(1, 1)).count == 10);
    CHECK(shatterPlan(size(1, 1)).soundMaterial == material::kGlassSmall);
    CHECK(shatterPlan(size(0, 3)).count == 10); // under 2: the small shatter
    CHECK(shatterPlan(size(0, 3)).shardSize == Catch::Approx(0.06F));
    CHECK(shatterPlan(size(2, 2)).shardSize == Catch::Approx(0.2F));
    CHECK(shatterPlan(size(2, 1)).count == 20);
}

TEST_CASE("a spawned pane: centre, normal, size word, two-sided GLASS triangles", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    panes.setType(1, GlassType{.windowLink = false, .alarm = false, .sprite = 0x00030005, .brokenSprite = 6});
    const GlassPane& made = panes.spawn(fixture.handle(), pane(1), fixture.world);
    CHECK(made.centre.y == Catch::Approx(12.0F));
    CHECK(made.centre.z == Catch::Approx(1.0F));
    CHECK(made.width == Catch::Approx(2.0F));
    CHECK(made.sizeWord == (2U | (2U << 16U)));
    CHECK(std::abs(made.normal.x) == Catch::Approx(1.0F));
    CHECK(made.sprite == 5); // the low 16 bits
    CHECK(made.colour == coney::world_objects::kPaneColour);
    const auto triangle = fixture.mesh->triangles()[2];
    CHECK((triangle.flags & coney::raycast::kTriangleTwoSided) != 0);
    CHECK(triangle.material == material::kGlass);
    CHECK(fixture.enabled(2));
    CHECK(panes.findByTriangle(3) == panes.find(made.handle));
}

TEST_CASE("the first hit breaks a pane, once", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    panes.setType(1, GlassType{.brokenSprite = 6});
    const double handle = panes.spawn(fixture.handle(), pane(1), fixture.world).handle;
    CHECK(panes.humanHit(handle, 42.0, fixture.world));
    const GlassPane& broken = *panes.find(handle);
    CHECK(broken.broken);
    CHECK_FALSE(broken.hidden);
    CHECK(broken.sprite == 6);
    CHECK_FALSE(fixture.enabled(2));
    CHECK_FALSE(fixture.enabled(3));
    REQUIRE(fixture.services.pairs.size() == 1);
    CHECK(fixture.services.pairs[0].first == material::kGlass);
    CHECK(fixture.services.shards > 0);
    CHECK(fixture.services.shards <= 2 * 40);
    CHECK(fixture.services.panesCounted == std::vector<double>{42.0});
    REQUIRE(fixture.services.bodies.size() == 1);
    CHECK_FALSE(fixture.services.bodies[0].second);
    // The window-look flags within the larger side go.
    REQUIRE(fixture.services.flagsDisabled.size() == 1);
    CHECK(fixture.services.flagsDisabled[0].first == Catch::Approx(2.0F));
    CHECK(fixture.services.flagsDisabled[0].second == coney::world_objects::kActivityWindowLook);
    // No alarm: no crime.
    CHECK(fixture.services.crimes.empty());

    // A second hit does nothing.
    CHECK_FALSE(panes.humanHit(handle, 42.0, fixture.world));
    CHECK(fixture.services.pairs.size() == 1);
}

TEST_CASE("a type with no broken sprite hides the pane; no shards when they are not wanted", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    fixture.services.wantShards = false;
    GlassPanes panes;
    const double handle = panes.spawn(fixture.handle(), pane(3), fixture.world).handle;
    CHECK(panes.hit(handle, fixture.world));
    CHECK(panes.find(handle)->hidden);
    CHECK(fixture.services.shards == 0);
    CHECK(fixture.services.pairs.size() == 1); // the sound plays anyway
}

TEST_CASE("an alarmed pane reports a break-in where it was broken", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    panes.setType(2, GlassType{.alarm = true});
    const double handle = panes.spawn(fixture.handle(), pane(2), fixture.world).handle;
    CHECK(panes.find(handle)->alarmBits == 3);
    panes.thrownHit(handle, 7.0, fixture.world);
    REQUIRE(fixture.services.crimes.size() == 1);
    CHECK(fixture.services.crimes[0].type == coney::world_objects::kCrimeBreakIn);
    CHECK(fixture.services.crimes[0].offender == 7.0);
    CHECK(fixture.services.crimeSceneMoves == 1);
}

TEST_CASE("a window link: avoided and charged through until the pane breaks", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    panes.setType(4, GlassType{.windowLink = true});
    const double handle = panes.spawn(fixture.handle(), pane(4), fixture.world).handle;
    // The corridor's choke link: avoided and, the pane lying in the corridor, retagged 0x40 both ways.
    CHECK(fixture.paths.edges()[2].avoid);
    CHECK(fixture.paths.edges()[2].flags == 0x40);
    CHECK(fixture.paths.edges()[3].flags == 0x40);
    panes.humanHit(handle, 1.0, fixture.world);
    CHECK_FALSE(fixture.paths.edges()[2].avoid);
    CHECK((fixture.paths.polygons()[2].flags & coney::world::kPathPolygonExcluded) != 0);
}

TEST_CASE("BreakGlassInRadius breaks the panes near, without the alarm", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    panes.setType(2, GlassType{.alarm = true});
    const double near = panes.spawn(fixture.handle(), pane(2), fixture.world).handle;
    const double far = panes.spawn(fixture.handle(), pane(2, 40.0F), fixture.world).handle;
    CHECK(panes.breakInRadius({4.0F, 12.0F, 1.0F}, 3.0F, fixture.world) == 1);
    CHECK(panes.find(near)->broken);
    CHECK_FALSE(panes.find(far)->broken);
    CHECK(fixture.services.crimes.empty());
    CHECK(fixture.services.glassObjectBreaks == 1);
}

TEST_CASE("pre-broken types, the invisible type and the car window", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    const double gap =
        panes.spawn(fixture.handle(), pane(coney::world_objects::glass_type::kPreBrokenGap), fixture.world).handle;
    CHECK(panes.find(gap)->broken);
    CHECK_FALSE(fixture.enabled(2));
    CHECK((fixture.paths.polygons()[2].flags & coney::world::kPathPolygonExcluded) != 0);
    CHECK_FALSE(panes.hit(gap, fixture.world)); // already broken

    const double invisible =
        panes.spawn(fixture.handle(), pane(coney::world_objects::glass_type::kInvisible, 30.0F), fixture.world).handle;
    CHECK(panes.find(invisible)->colour == 0);

    const double window =
        panes.spawn(fixture.handle(), pane(coney::world_objects::glass_type::kCarWindow, 50.0F), fixture.world).handle;
    CHECK(panes.hit(window, fixture.world));
    CHECK(fixture.services.stereosFreed == 1);
}

TEST_CASE("message 0 shatters without breaking", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    const double handle = panes.spawn(fixture.handle(), pane(1), fixture.world).handle;
    panes.shatterOnly(handle, fixture.world);
    CHECK_FALSE(panes.find(handle)->broken);
    CHECK_FALSE(fixture.enabled(2));
    CHECK(fixture.services.pairs.size() == 1);
}

TEST_CASE("the panes drawn: whole ones within 50 m as a quad of their rectangle, the near ones last",
          "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    panes.setType(1, GlassType{.windowLink = false, .alarm = false, .sprite = 0x00010014, .brokenSprite = 0x15});
    panes.setType(17, GlassType{.windowLink = false, .alarm = false, .sprite = 0x15, .brokenSprite = 0x15});
    const double near = panes.spawn(fixture.handle(), pane(1), fixture.world).handle;
    const double far = panes.spawn(fixture.handle(), pane(1, 31.0F), fixture.world).handle;
    const double gone = panes.spawn(fixture.handle(), pane(1, 61.0F), fixture.world).handle;
    const double preBroken = panes.spawn(fixture.handle(), pane(17, 21.0F), fixture.world).handle;
    const double broken = panes.spawn(fixture.handle(), pane(1, 41.0F), fixture.world).handle;
    panes.hit(broken, fixture.world);

    // From (5, 12, 1): the first pane 1 m away is near; the one at y 62 is beyond 50 m; the broken one is not drawn.
    const std::vector<coney::world_objects::GlassQuad> quads =
        coney::world_objects::glassDraws(panes, coney::anim::Vec3{5.0F, 12.0F, 1.0F});
    REQUIRE(quads.size() == 3);
    CHECK(quads[0].handle == far);
    CHECK(quads[1].handle == preBroken);
    CHECK(quads[2].handle == near);
    CHECK(quads[2].rect == 0x14); // the high half ignored
    CHECK(quads[1].rect == 0x15);
    CHECK(quads[2].colour == coney::world_objects::kPaneColour);
    // The corners: the first, along the width, along the height, the opposite one.
    CHECK(quads[2].corners[0].y == Catch::Approx(11.0F));
    CHECK(quads[2].corners[0].z == Catch::Approx(0.0F));
    CHECK(quads[2].corners[1].y == Catch::Approx(13.0F));
    CHECK(quads[2].corners[2].z == Catch::Approx(2.0F));
    CHECK(quads[2].corners[3].y == Catch::Approx(13.0F));
    CHECK(quads[2].corners[3].z == Catch::Approx(2.0F));
    CHECK(panes.find(broken)->colour == coney::world_objects::kBrokenPaneColour);
    static_cast<void>(gone);
}

TEST_CASE("a whole pane's body is a box 0.25 m proud of the glass; a broken pane has none", "[world_objects][glass]") {
    coney::test::ObjectWorldFixture fixture;
    GlassPanes panes;
    panes.setType(1, GlassType{.brokenSprite = 6});
    // The pane in the plane x = 4, y 11 to 13, z 0 to 2.
    const double handle = panes.spawn(fixture.handle(), pane(1), fixture.world).handle;
    const std::vector<double> touching{handle};
    // A 0.5 m sphere 0.7 m from the glass reaches the box (0.25 + 0.5 > 0.7); 0.8 m away it does not.
    CHECK(panes.bodiesTouching({4.7F, 12.0F, 1.0F}, 0.5F) == touching);
    CHECK(panes.bodiesTouching({3.3F, 12.0F, 1.0F}, 0.5F) == touching); // from either side
    CHECK(panes.bodiesTouching({4.8F, 12.0F, 1.0F}, 0.5F).empty());
    // Past the pane's edge by more than the radius: nothing; above its top within the radius: touching.
    CHECK(panes.bodiesTouching({4.0F, 13.6F, 1.0F}, 0.5F).empty());
    CHECK(panes.bodiesTouching({4.0F, 12.0F, 2.4F}, 0.5F) == touching);
    // Broken, the body is gone.
    REQUIRE(panes.humanHit(handle, 42.0, fixture.world));
    CHECK(panes.bodiesTouching({4.0F, 12.0F, 1.0F}, 0.5F).empty());
}
