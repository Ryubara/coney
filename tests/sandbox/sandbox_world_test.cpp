// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_world.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "raycast/collision_mesh.h"
#include "sandbox/sandbox_layout.h"

using Catch::Approx;
using coney::raycast::Ray;
using coney::raycast::Vec3;
using coney::sandbox::SandboxWorld;

namespace {

// The hit of a cast that must hit.
coney::raycast::RayHit mustHit(const std::optional<coney::raycast::RayHit>& hit) {
    REQUIRE(hit.has_value());
    return hit.value_or(coney::raycast::RayHit{});
}

// A small movement course, synthetic: a floor, a 30° ramp, stairs, a wall and a tall box casting a shadow.
constexpr std::string_view kCourse = R"(
sun direction=0,0.6,-0.8
box at=0,0,-0.5 size=60,60,0.5
ramp at=0,10,0 width=3 height=2 angle=30
stairs at=-10,10,0 width=2 steps=5 rise=0.2 run=0.3
box at=10,0,0 size=1,6,3
box at=-10,-10,0 size=2,2,8
)";

// The world of `text`, which must build.
SandboxWorld worldOf(std::string_view text) {
    auto layout = coney::sandbox::parseSandboxLayout(text);
    REQUIRE(layout.has_value());
    auto world = SandboxWorld::build(std::move(*layout));
    REQUIRE(world.has_value());
    return std::move(*world);
}

// A ray straight down from `top` at (x, y), 20 m long.
Ray down(float x, float y, float top = 10.0F) { return Ray{{x, y, top}, coney::raycast::kDown, 20.0F}; }

// The colour of the drawn vertex nearest `p` whose normal is close to `normal`.
coney::sandbox::Colour colourNear(const SandboxWorld& world, coney::anim::Vec3 p, coney::anim::Vec3 normal) {
    const coney::sandbox::MeshVertex* best = nullptr;
    float bestDistance = 1e9F;
    for (const auto& v : world.mesh().vertices) {
        const float d = coney::anim::distance(v.position, p);
        if (coney::anim::dot(v.normal, normal) > 0.9F && d < bestDistance) {
            best = &v;
            bestDistance = d;
        }
    }
    REQUIRE(best != nullptr);
    return best->colour;
}

} // namespace

TEST_CASE("the sandbox's collision mesh holds the floor, the slope, the stair tops and the walls", "[sandbox][world]") {
    const SandboxWorld world = worldOf(kCourse);
    const coney::raycast::CollisionMesh* mesh = world.collision();
    REQUIRE(mesh != nullptr);

    // The floor, at z = 0.
    const coney::raycast::RayHit floor = mustHit(mesh->rayCast(down(20.0F, -20.0F), {}, 0));
    CHECK(10.0F - floor.t == Approx(0.0F).margin(1e-5));
    CHECK(floor.normal.z == Approx(1.0F));
    CHECK(floor.material == coney::raycast::kMaterialConcrete);

    // Half way up the 30° ramp (its run is 2 / tan 30° = 3.46 m, centred on y = 10): 1 m up, facing 30° from up.
    const coney::raycast::RayHit slope = mustHit(mesh->rayCast(down(0.0F, 10.0F), {}, 0));
    CHECK(10.0F - slope.t == Approx(1.0F).margin(1e-4));
    CHECK(slope.normal.z == Approx(std::cos(0.5235988F)).margin(1e-4));
    CHECK(slope.normal.y == Approx(-0.5F).margin(1e-4));

    // Each stair's tread: the stairs run from y = 9.25 to 10.75, 0.3 m a step, 0.2 m up each.
    for (int step = 0; step < 5; ++step) {
        const float y = 9.25F + 0.3F * (static_cast<float>(step) + 0.5F);
        const coney::raycast::RayHit tread = mustHit(mesh->rayCast(down(-10.0F, y), {}, 0));
        CHECK(10.0F - tread.t == Approx(0.2F * static_cast<float>(step + 1)).margin(1e-4));
    }

    // The wall at x = 9.5..10.5 stops a ray from the west, at its face.
    const coney::raycast::RayHit wall =
        mustHit(mesh->rayCast(Ray{{0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, 20.0F}, {}, 0));
    CHECK(wall.t == Approx(9.5F));
    CHECK(wall.normal.x == Approx(-1.0F));
    // A ray over the top of it goes on.
    CHECK_FALSE(mesh->rayCast(Ray{{0.0F, 0.0F, 3.5F}, {1.0F, 0.0F, 0.0F}, 20.0F}, {}, 0).has_value());

    // A point dropped onto the stairs lands on a tread.
    Vec3 p{-10.0F, 10.5F, 5.0F};
    REQUIRE(coney::raycast::dropToGround(*mesh, 10.0F, p));
    CHECK(p.z == Approx(1.0F + 0.1F).margin(1e-4));
}

TEST_CASE("the baked light shades by facing, shadows behind a tall box and darkens contact corners",
          "[sandbox][world]") {
    const SandboxWorld world = worldOf(kCourse);
    CHECK(world.bakeStats().shadowed > 0);
    CHECK(world.bakeStats().occlusionRays > 0);

    // The sun travels towards +y and down, so the tall box at (-10, -10) shades the floor north of it.
    const auto open = colourNear(world, {-20.0F, -20.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    const auto shaded = colourNear(world, {-10.0F, -7.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    CHECK(shaded.r < open.r - 0.2F);

    // A wall the sun strikes at a slant (-y, 0.6) is darker than the roof it strikes more squarely (0.8).
    const auto roof = colourNear(world, {-10.0F, -10.0F, 8.0F}, {0.0F, 0.0F, 1.0F});
    const auto north = colourNear(world, {-10.0F, -11.0F, 4.0F}, {0.0F, -1.0F, 0.0F});
    CHECK(north.g < roof.g);

    // Half a metre from the foot of the long wall the floor is darker than in the open a few metres away.
    const auto foot = colourNear(world, {9.0F, -2.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    const auto clear = colourNear(world, {6.0F, -2.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    CHECK(foot.b < clear.b);
}

TEST_CASE("the floor under a box is dark, so it never glows along the box's foot", "[sandbox][world]") {
    const SandboxWorld world = worldOf("box at=0,0,-0.5 size=20,20,0.5\nbox at=0.5,0.5,0 size=4,4,2\n");
    const auto under = colourNear(world, {1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    const auto open = colourNear(world, {-8.0F, -8.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    CHECK(under.r < open.r * 0.5F);
}

TEST_CASE("baking is deterministic", "[sandbox][world]") {
    const SandboxWorld a = worldOf(kCourse);
    const SandboxWorld b = worldOf(kCourse);
    REQUIRE(a.mesh().vertices.size() == b.mesh().vertices.size());
    for (std::size_t i = 0; i < a.mesh().vertices.size(); ++i) {
        REQUIRE(a.mesh().vertices[i].colour == b.mesh().vertices[i].colour);
    }
}

TEST_CASE("a layout with nothing solid has no collision mesh", "[sandbox][world]") {
    const SandboxWorld world = worldOf("box at=0,0,0 size=1,1,1 solid=no\n");
    CHECK(world.collision() == nullptr);
    CHECK_FALSE(world.mesh().triangles.empty());
}

TEST_CASE("the shipped layouts parse and build", "[sandbox][world]") {
    const std::filesystem::path folder = std::filesystem::path(CONEY_ASSETS_DIR) / "sandbox";
    const std::vector<std::string> names = coney::sandbox::listSandboxLayouts(folder);
    CHECK(std::ranges::find(names, "default") != names.end());
    CHECK(std::ranges::find(names, "parkour") != names.end());
    for (const std::string& name : names) {
        INFO(name);
        auto world = SandboxWorld::load(folder, name);
        REQUIRE(world.has_value());
        CHECK(world->collision() != nullptr);
        CHECK(world->folder() == folder);
        // Every texture a layout names is beside it.
        for (const auto& texture : world->layout().textures) {
            CHECK(std::filesystem::is_regular_file(folder / texture.file));
        }
        // Every spawn stands on something.
        for (const auto& spawn : world->layout().spawns) {
            Vec3 p{spawn.position.x, spawn.position.y, spawn.position.z + 1.0F};
            CHECK(coney::raycast::dropToGround(*world->collision(), 3.0F, p));
        }
    }
}
