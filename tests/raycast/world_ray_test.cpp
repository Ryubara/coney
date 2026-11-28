// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "raycast/collision_builder.h"
#include "raycast/collision_mesh.h"
#include "raycast/materials.h"

// Physics Unit 16: the material names and the world manager's ray (docs/research/physics-world.md#unit-16).

using Catch::Approx;
using coney::raycast::BuildTriangle;
using coney::raycast::CollisionMesh;
using coney::raycast::Ray;
using coney::raycast::RayResult;
using coney::raycast::Vec3;

namespace {

constexpr std::uint8_t kGlass = 2; // MATERIAL_GLASS
constexpr std::uint8_t kSteel = 3; // MATERIAL_STEEL

// A two-sided pane of glass in the plane x = `x`, across the ray (0, 0, 1) along +x; its own normal faces +x.
BuildTriangle pane(float x) {
    return BuildTriangle{.corners = {Vec3{x, -1.0F, 0.0F}, Vec3{x, 1.0F, 0.0F}, Vec3{x, 0.0F, 3.0F}},
                         .flags = coney::raycast::kTriangleTwoSided,
                         .material = kGlass};
}

// A one-sided steel wall in the plane x = `x`, facing -x, across the same ray.
BuildTriangle wall(float x) {
    return BuildTriangle{.corners = {Vec3{x, -1.0F, 0.0F}, Vec3{x, 0.0F, 3.0F}, Vec3{x, 1.0F, 0.0F}},
                         .material = kSteel};
}

// A mesh of `triangles` in one cell, so a cell's list holds them in this order.
std::unique_ptr<CollisionMesh> oneCell(const std::vector<BuildTriangle>& triangles) {
    auto mesh = coney::raycast::buildCollisionMesh(triangles, 100.0F);
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

// The ray every case casts: from (0, 0, 1) along +x, length 5.
constexpr Ray kAlongX{.origin = {0.0F, 0.0F, 1.0F}, .direction = {1.0F, 0.0F, 0.0F}, .length = 5.0F};

// A result as a caller might leave it before a cast: t 7, normal (0, 0, 9), triangle 42.
RayResult prefilled() {
    RayResult result;
    result.hit = true;
    result.t = 7.0F;
    result.normal = Vec3{0.0F, 0.0F, 9.0F};
    result.material = 77;
    result.flags = 0x123;
    result.area = 9;
    result.triangle = 42;
    return result;
}

} // namespace

TEST_CASE("material ids name the game's materials, and any other id is unknown", "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-names, confirmed (code)
    using coney::raycast::materialName;
    CHECK(materialName(0) == "undefined");
    CHECK(materialName(1) == "MATERIAL_NONE");
    CHECK(materialName(2) == "MATERIAL_GLASS");
    CHECK(materialName(116) == "MATERIAL_BRICKWALL");
    CHECK(materialName(190) == "MATERIAL_SALAMI");
    // Expected: physics-world.md#u16-material-name, confirmed (code)
    CHECK(materialName(191) == "unknown");
    CHECK(materialName(255) == "unknown");
    CHECK(materialName(-1) == "unknown");
    std::set<std::string, std::less<>> names;
    for (int id = 0; id <= 190; ++id) {
        names.emplace(materialName(id));
    }
    CHECK(names.size() == 191);
    CHECK_FALSE(names.contains("unknown"));
}

TEST_CASE("the world ray presets only the material and the hit, leaving the rest as the caller had it",
          "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-world-ray, confirmed (code)
    // A floor far below the ray: nothing crossed.
    const auto mesh = oneCell({BuildTriangle{
        .corners = {Vec3{-10.0F, -10.0F, -50.0F}, Vec3{10.0F, -10.0F, -50.0F}, Vec3{10.0F, 10.0F, -50.0F}}}});
    RayResult result = prefilled();
    coney::raycast::worldRayCast(mesh.get(), kAlongX, result, {}, 0);
    CHECK_FALSE(result.hit);
    CHECK(result.material == coney::raycast::kMaterialNone);
    CHECK(result.t == 7.0F);
    CHECK(result.normal == Vec3{0.0F, 0.0F, 9.0F});
    CHECK(result.flags == 0x123);
    CHECK(result.area == 9);
    CHECK(result.triangle == 42);
}

TEST_CASE("the world ray with no level reports nothing crossed", "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-coney, confirmed (code)
    RayResult result = prefilled();
    coney::raycast::worldRayCast(nullptr, kAlongX, result, {}, 0);
    CHECK_FALSE(result.hit);
    CHECK(result.material == coney::raycast::kMaterialNone);
    CHECK(result.t == 7.0F);
}

TEST_CASE("an excluded pane the world ray crosses fills the result without a hit", "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-world-ray, confirmed (code)
    const auto mesh = oneCell({pane(1.0F)});
    const std::array<std::uint8_t, 1> glass{kGlass};
    RayResult result = prefilled();
    coney::raycast::worldRayCast(mesh.get(), kAlongX, result, glass, 0);
    CHECK_FALSE(result.hit);
    CHECK(result.material == kGlass);
    CHECK(result.t == Approx(1.0F));
    CHECK(result.triangle == 0);
    CHECK(result.flags == (coney::raycast::kTriangleTwoSided | coney::raycast::kTriangleEnabled));
    CHECK(result.normal == Vec3{0.0F, 0.0F, 9.0F}); // still the caller's
}

TEST_CASE("the same pane with no exclusion is a hit with the normal facing the ray", "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-world-ray, confirmed (code)
    const auto mesh = oneCell({pane(1.0F)});
    RayResult result = prefilled();
    coney::raycast::worldRayCast(mesh.get(), kAlongX, result, {}, 0);
    CHECK(result.hit);
    CHECK(result.material == kGlass);
    CHECK(result.t == Approx(1.0F));
    CHECK(result.normal.x == Approx(-1.0F));
    CHECK(result.normal.y == Approx(0.0F));
    CHECK(result.normal.z == Approx(0.0F));
}

TEST_CASE("a kept wall behind an excluded pane becomes the world ray's hit", "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-world-ray, confirmed (code)
    const std::array<std::uint8_t, 1> glass{kGlass};
    SECTION("the pane listed first") {
        const auto mesh = oneCell({pane(1.0F), wall(3.0F)});
        RayResult result;
        coney::raycast::worldRayCast(mesh.get(), kAlongX, result, glass, 0);
        CHECK(result.hit);
        CHECK(result.t == Approx(3.0F));
        CHECK(result.material == kSteel);
    }
    SECTION("the wall listed first: an excluded triangle never replaces a kept hit") {
        const auto mesh = oneCell({wall(3.0F), pane(1.0F)});
        RayResult result;
        coney::raycast::worldRayCast(mesh.get(), kAlongX, result, glass, 0);
        CHECK(result.hit);
        CHECK(result.t == Approx(3.0F));
        CHECK(result.material == kSteel);
    }
}

TEST_CASE("of two excluded panes the world ray keeps the last one tested, not the nearest",
          "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-tests, confirmed (code)
    const std::array<std::uint8_t, 1> glass{kGlass};
    SECTION("x = 2 listed first, x = 1 second") {
        const auto mesh = oneCell({pane(2.0F), pane(1.0F)});
        RayResult result;
        coney::raycast::worldRayCast(mesh.get(), kAlongX, result, glass, 0);
        CHECK_FALSE(result.hit);
        CHECK(result.t == Approx(1.0F));
    }
    SECTION("x = 1 listed first, x = 2 second") {
        const auto mesh = oneCell({pane(1.0F), pane(2.0F)});
        RayResult result;
        coney::raycast::worldRayCast(mesh.get(), kAlongX, result, glass, 0);
        CHECK_FALSE(result.hit);
        CHECK(result.t == Approx(2.0F));
    }
}

TEST_CASE("the world ray's mask skips a triangle by its type bits", "[collision][physics-unit-16]") {
    // Expected: physics-world.md#u16-tests, confirmed (code)
    const auto mesh = oneCell({BuildTriangle{
        .corners = {Vec3{-10.0F, -10.0F, 0.0F}, Vec3{10.0F, -10.0F, 0.0F}, Vec3{10.0F, 10.0F, 0.0F}}, .flags = 0x08}});
    const Ray down{.origin = {5.0F, -2.0F, 1.0F}, .direction = coney::raycast::kDown, .length = 5.0F};
    RayResult masked;
    coney::raycast::worldRayCast(mesh.get(), down, masked, {}, 8);
    CHECK_FALSE(masked.hit);
    CHECK(masked.material == coney::raycast::kMaterialNone);
    RayResult open;
    coney::raycast::worldRayCast(mesh.get(), down, open, {}, 0);
    CHECK(open.hit);
    CHECK(open.t == Approx(1.0F));
}
