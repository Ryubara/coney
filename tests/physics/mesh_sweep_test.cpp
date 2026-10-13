// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/mesh_sweep.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "raycast/collision_builder.h"

// The page's synthetic meshes (docs/research/physics-bodies.md#u2-test-ideas-synthetic-meshes-src-raycast), built with
// Coney's own collision builder, cell size 2 m. No game data.

using Catch::Approx;
using coney::anim::Vec3;
using coney::physics::Box;
using coney::physics::CapsuleSweep;
using coney::physics::Contact;
using coney::physics::MeshContact;
using coney::physics::MeshScratch;
using coney::physics::Sphere;
using coney::physics::SphereSweep;
using coney::raycast::BuildTriangle;

namespace {

constexpr double kTolerance = 1e-5;

using Rv = coney::raycast::Vec3;

// A mesh from `triangles`, required to build.
std::unique_ptr<coney::raycast::CollisionMesh> meshOf(const std::vector<BuildTriangle>& triangles) {
    auto mesh = coney::raycast::buildCollisionMesh(triangles, 2.0F);
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

// The page's wall: the plane x = `x`, y −5..5, z 0..3 in two triangles, its front facing −x.
std::vector<BuildTriangle> wallFacingMinusX(float x, std::uint16_t flags = 0) {
    return {BuildTriangle{.corners = {Rv{x, -5, 0}, Rv{x, 5, 3}, Rv{x, 5, 0}}, .flags = flags},
            BuildTriangle{.corners = {Rv{x, -5, 0}, Rv{x, -5, 3}, Rv{x, 5, 3}}, .flags = flags}};
}

// A narrow upright face at x = 0, 0.1 m wide and `height` tall, facing −x: its longest edge is steep, so only the low
// rule can skip it.
std::vector<BuildTriangle> narrowFace(float height) {
    return {BuildTriangle{.corners = {Rv{0, -0.05F, 0}, Rv{0, 0.05F, height}, Rv{0, 0.05F, 0}}},
            BuildTriangle{.corners = {Rv{0, -0.05F, 0}, Rv{0, -0.05F, height}, Rv{0, 0.05F, height}}}};
}

// The box a sphere at `centre` of `radius` covers over the move.
Box sweptBox(Vec3 centre, float radius, Vec3 move) {
    const Vec3 lo{std::min(centre.x, centre.x + move.x) - radius, std::min(centre.y, centre.y + move.y) - radius,
                  std::min(centre.z, centre.z + move.z) - radius};
    const Vec3 hi{std::max(centre.x, centre.x + move.x) + radius, std::max(centre.y, centre.y + move.y) + radius,
                  std::max(centre.z, centre.z + move.z) + radius};
    return Box{lo, hi};
}

// The walking capsule's contacts for feet at `feet` moving by `move`, sphere radius 0.5.
std::vector<Contact> capsuleContacts(const coney::raycast::CollisionMesh& mesh, Vec3 feet, Vec3 move,
                                     bool airborne = false) {
    const CapsuleSweep sweep{.feet = feet, .radius = 0.5F, .move = move, .ownerAirborne = airborne};
    MeshScratch scratch;
    std::vector<Contact> out;
    const int count = coney::physics::sweepCapsuleMesh(mesh, sweptBox(Vec3{feet.x, feet.y, feet.z + 0.55F}, 0.5F, move),
                                                       sweep, scratch, out);
    CHECK(count == static_cast<int>(out.size()));
    return out;
}

// Checks two vectors match to the tolerance.
void checkVec(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == Approx(expected.x).margin(kTolerance));
    CHECK(actual.y == Approx(expected.y).margin(kTolerance));
    CHECK(actual.z == Approx(expected.z).margin(kTolerance));
}

} // namespace

TEST_CASE("a wall stops a fast capsule where it first touches, once per triangle", "[physics][mesh_sweep]") {
    const auto mesh = meshOf(wallFacingMinusX(0.0F));
    // Ten metres in one call: the wall is met at (2 − 0.5) / 10, by one triangle though it lies in many cells.
    const auto contacts = capsuleContacts(*mesh, Vec3{-2, 0, 0}, Vec3{10, 0, 0});
    REQUIRE(contacts.size() == 1);
    CHECK(contacts[0].fraction == Approx(0.15F).margin(kTolerance));
    checkVec(contacts[0].normal, Vec3{-1, 0, 0});
    checkVec(contacts[0].point, Vec3{-0.5F, 0, 0});
    checkVec(contacts[0].moveDirection, Vec3{1, 0, 0});
    REQUIRE(contacts[0].triangle.has_value());
}

TEST_CASE("a capsule starting behind a one-sided wall passes it, a two-sided one turns", "[physics][mesh_sweep]") {
    CHECK(capsuleContacts(*meshOf(wallFacingMinusX(0.0F)), Vec3{1, 0, 0}, Vec3{-3, 0, 0}).empty());
    const auto contacts = capsuleContacts(*meshOf(wallFacingMinusX(0.0F, coney::raycast::kTriangleTwoSided)),
                                          Vec3{1, 0, 0}, Vec3{-3, 0, 0});
    REQUIRE(contacts.size() == 1);
    checkVec(contacts[0].normal, Vec3{1, 0, 0});
    CHECK(contacts[0].fraction == Approx(0.5F / 3.0F).margin(kTolerance));
}

TEST_CASE("a capsule moving away, along or not at all meets nothing", "[physics][mesh_sweep]") {
    const auto mesh = meshOf(wallFacingMinusX(0.0F));
    CHECK(capsuleContacts(*mesh, Vec3{-2, 0, 0}, Vec3{-1, 0, 0}).empty());
    CHECK(capsuleContacts(*mesh, Vec3{-2, 0, 0}, Vec3{0, 1, 0}).empty());
    CHECK(capsuleContacts(*mesh, Vec3{-0.6F, 0, 0}, Vec3{0, 0, 0}).empty());
}

TEST_CASE("the capsule's low rule: a face rising less than 0.25 m is not a wall", "[physics][mesh_sweep]") {
    // The face is two triangles, each a wall from 0.25 m, so a face that counts gives a contact for each.
    CHECK(capsuleContacts(*meshOf(narrowFace(0.24F)), Vec3{-2, 0, 0}, Vec3{3, 0, 0}).empty());
    CHECK(!capsuleContacts(*meshOf(narrowFace(0.25F)), Vec3{-2, 0, 0}, Vec3{3, 0, 0}).empty());
    CHECK(!capsuleContacts(*meshOf(narrowFace(0.26F)), Vec3{-2, 0, 0}, Vec3{3, 0, 0}).empty());
}

TEST_CASE("the capsule's thin rule: a sliver along a slanted longest edge is not a wall", "[physics][mesh_sweep]") {
    // The longest edge rises 1 m over 4 (|unit z| 0.24, under 0.8); the third corner lies `off` from its line, half
    // way along, so the steepest edge (to that corner) rises well over 0.25 m.
    const auto sliver = [](float off) {
        const Vec3 across{0.0F, -0.242536F, 0.970143F};
        const Rv c{0.0F, across.y * off, 0.5F + (across.z * off)};
        return std::vector<BuildTriangle>{BuildTriangle{.corners = {Rv{0, -2, 0}, c, Rv{0, 2, 1}}}};
    };
    CHECK(capsuleContacts(*meshOf(sliver(0.2F)), Vec3{-2, 0, 0}, Vec3{3, 0, 0}).empty());
    CHECK(capsuleContacts(*meshOf(sliver(0.3F)), Vec3{-2, 0, 0}, Vec3{3, 0, 0}).size() == 1);
}

TEST_CASE("the capsule's walls are faces with |n.z| up to 0.65", "[physics][mesh_sweep]") {
    // A face leaning back so that its normal's z is about `nz`, facing −x.
    const auto leaning = [](float nz) {
        const float lean = nz / std::sqrt(1.0F - (nz * nz)); // x per metre of z
        return std::vector<BuildTriangle>{
            BuildTriangle{.corners = {Rv{0, -5, 0}, Rv{3 * lean, 5, 3}, Rv{0, 5, 0}}},
            BuildTriangle{.corners = {Rv{0, -5, 0}, Rv{3 * lean, -5, 3}, Rv{3 * lean, 5, 3}}}};
    };
    CHECK_FALSE(capsuleContacts(*meshOf(leaning(0.649F)), Vec3{-2, 0, 0}, Vec3{3, 0, 0}).empty());
    CHECK(capsuleContacts(*meshOf(leaning(0.651F)), Vec3{-2, 0, 0}, Vec3{3, 0, 0}).empty());
}

TEST_CASE("the side is decided from the centre at the start", "[physics][mesh_sweep]") {
    // The centre 1e-7 in front of the plane counts as behind: one-sided passed, two-sided turned.
    const float x = 0.5F + 1e-7F;
    CHECK(capsuleContacts(*meshOf(wallFacingMinusX(x)), Vec3{0.5F, 0, 0}, Vec3{0.3F, 0, 0}).empty());
    const auto turned = capsuleContacts(*meshOf(wallFacingMinusX(x, coney::raycast::kTriangleTwoSided)),
                                        Vec3{0.5F, 0, 0}, Vec3{-0.3F, 0, 0});
    REQUIRE(turned.size() == 1);
    checkVec(turned[0].normal, Vec3{1, 0, 0});
}

TEST_CASE("a disabled triangle is never met", "[physics][mesh_sweep]") {
    auto mesh = meshOf(wallFacingMinusX(0.0F));
    for (auto& triangle : mesh->mutableTriangles()) {
        triangle.flags = static_cast<std::uint16_t>(triangle.flags & ~coney::raycast::kTriangleEnabled);
    }
    CHECK(capsuleContacts(*mesh, Vec3{-2, 0, 0}, Vec3{10, 0, 0}).empty());
}

TEST_CASE("a query box past the grid clamps to its last cells", "[physics][mesh_sweep]") {
    const auto mesh = meshOf(wallFacingMinusX(0.0F));
    MeshScratch scratch;
    const auto cells =
        coney::physics::beginShapeQuery(*mesh, Box{Vec3{-1000, -1000, -1000}, Vec3{1000, 1000, 1000}}, scratch);
    const auto size = mesh->cells();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CHECK(cells.min.at(axis) == 0);
        CHECK(cells.max.at(axis) == size.at(axis) - 1);
    }
}

TEST_CASE("the mesh sort smears an insertion before the end; the contact insert does not", "[physics][mesh_sweep]") {
    std::vector<MeshContact> found(3);
    found[0].key = 0.5F;
    found[1].key = 0.6F;
    found[2].key = 0.3F;
    std::vector<std::size_t> order;
    coney::physics::sortByFraction(found, order);
    CHECK(order == std::vector<std::size_t>{2, 0, 0});
    found.resize(2);
    found[0].key = 0.3F;
    found[1].key = 0.3F;
    coney::physics::sortByFraction(found, order);
    CHECK(order == std::vector<std::size_t>{0, 1});

    std::array<Contact, 3> contacts{};
    contacts[0].fraction = 0.2F;
    contacts[1].fraction = 0.4F;
    contacts[2].fraction = 0.4F;
    std::vector<Contact*> sorted;
    for (Contact& contact : contacts) {
        coney::physics::insertSorted(contact, sorted, nullptr, nullptr, false);
    }
    CHECK(sorted == std::vector<Contact*>{&contacts[0], &contacts[1], &contacts[2]});
    sorted.clear();
    coney::physics::insertSorted(contacts[1], sorted, nullptr, nullptr, false);
    coney::physics::insertSorted(contacts[0], sorted, nullptr, nullptr, false);
    CHECK(sorted == std::vector<Contact*>{&contacts[0], &contacts[1]});
}

TEST_CASE("the corner slide points outward past the flagged end of a wall", "[physics][mesh_sweep]") {
    // A vertical triangle facing +x whose edge 2 runs down the wall's y = 0 end, named in bits 14-15.
    const auto end = [](std::uint16_t edge) {
        return std::vector<BuildTriangle>{BuildTriangle{.corners = {Rv{0, 0, 0}, Rv{0, 1, 0}, Rv{0, 0, 1}},
                                                        .flags = static_cast<std::uint16_t>(1U | (edge << 14U))}};
    };
    // Met past that end (the centre's path at y = −0.3): S is −y, max(0.5, |move|) long, and flagged.
    const auto contacts = capsuleContacts(*meshOf(end(2)), Vec3{1, -0.3F, -0.3F}, Vec3{-1, 0, 0});
    REQUIRE(contacts.size() == 1);
    checkVec(contacts[0].slide, Vec3{0, -1, 0});
    CHECK(contacts[0].flags == coney::physics::kContactAddSlide);
    // Airborne, no edge named, or a face hit: no slide.
    for (const auto& none : {capsuleContacts(*meshOf(end(2)), Vec3{1, -0.3F, -0.3F}, Vec3{-1, 0, 0}, true),
                             capsuleContacts(*meshOf(end(3)), Vec3{1, -0.3F, -0.3F}, Vec3{-1, 0, 0}),
                             capsuleContacts(*meshOf(end(2)), Vec3{1, 0.5F, -0.3F}, Vec3{-1, 0, 0})}) {
        REQUIRE(none.size() == 1);
        checkVec(none[0].slide, Vec3{0, 0, 0});
        CHECK(none[0].flags == 0);
    }
    // The mirrored end: an edge running up at y = 1 gives +y.
    const std::vector<BuildTriangle> other{
        BuildTriangle{.corners = {Rv{0, 1, 0}, Rv{0, 1, 1}, Rv{0, 0, 0}}, .flags = 1U}};
    const auto mirrored = capsuleContacts(*meshOf(other), Vec3{1, 1.3F, -0.3F}, Vec3{-1, 0, 0});
    REQUIRE(mirrored.size() == 1);
    checkVec(mirrored[0].slide, Vec3{0, 1, 0});
}

TEST_CASE("the sphere sweep's max, options and n.z range", "[physics][mesh_sweep]") {
    MeshScratch scratch;
    std::vector<Contact> out;
    // Two parallel walls: max 1 keeps the nearer.
    auto walls = wallFacingMinusX(0.0F);
    for (const auto& t : wallFacingMinusX(1.0F)) {
        walls.push_back(t);
    }
    const auto twoWalls = meshOf(walls);
    SphereSweep sweep{.sphere = Sphere{Vec3{-2, 0, 1}, 0.5F}, .move = Vec3{4, 0, 0}, .max = 1};
    REQUIRE(coney::physics::sweepSphereMesh(*twoWalls, sweptBox(Vec3{-2, 0, 1}, 0.5F, sweep.move), sweep, scratch,
                                            out) == 1);
    CHECK(out[0].fraction == Approx(1.5F / 4.0F).margin(kTolerance));
    CHECK(out[0].startDistance == Approx(2.0F).margin(kTolerance));

    // Behind a one-sided wall with option 0x40: kept, flagged behind, normal not turned.
    const auto wall = meshOf(wallFacingMinusX(0.0F));
    out.clear();
    sweep = SphereSweep{.sphere = Sphere{Vec3{0.3F, 0, 1}, 0.5F},
                        .move = Vec3{-0.2F, 0, 0},
                        .options = coney::physics::kQueryKeepAll,
                        .max = 4};
    REQUIRE(coney::physics::sweepSphereMesh(*wall, sweptBox(Vec3{0.3F, 0, 1}, 0.5F, sweep.move), sweep, scratch, out) >=
            1);
    CHECK((out[0].flags & coney::physics::kContactBehind) != 0);
    checkVec(out[0].normal, Vec3{-1, 0, 0});

    // Two coplanar triangles both reached at once: one per plane keeps one.
    out.clear();
    sweep = SphereSweep{.sphere = Sphere{Vec3{-0.3F, -5, 1.5F}, 0.5F},
                        .move = Vec3{0.1F, 0, 0},
                        .options = coney::physics::kQueryOnePerPlane,
                        .max = 4};
    CHECK(coney::physics::sweepSphereMesh(*wall, sweptBox(Vec3{-0.3F, -5, 1.5F}, 0.5F, sweep.move), sweep, scratch,
                                          out) == 1);

    // A ceiling (n.z = −1) under option 4 with a minimum of −0.65 is dropped.
    const auto ceiling = meshOf({BuildTriangle{.corners = {Rv{-5, -5, 2}, Rv{5, 5, 2}, Rv{5, -5, 2}}}});
    sweep = SphereSweep{.sphere = Sphere{Vec3{0, 0, 1}, 0.5F}, .move = Vec3{0, 0, 1}, .max = 1};
    out.clear();
    CHECK(coney::physics::sweepSphereMesh(*ceiling, sweptBox(Vec3{0, 0, 1}, 0.5F, sweep.move), sweep, scratch, out) ==
          1);
    sweep.options = coney::physics::kQueryMinNormalZ;
    sweep.minNormalZ = -0.65F;
    out.clear();
    CHECK(coney::physics::sweepSphereMesh(*ceiling, sweptBox(Vec3{0, 0, 1}, 0.5F, sweep.move), sweep, scratch, out) ==
          0);
}

TEST_CASE("the sphere overlap returns 1 and puts the deepest kept first, from either side", "[physics][mesh_sweep]") {
    // The centre 0.3 m from a wall at x = 0 and 0.1 m from one at x = −0.4 facing +x: depths 0.2 and 0.4.
    auto triangles = wallFacingMinusX(0.0F);
    triangles.push_back(BuildTriangle{.corners = {Rv{-0.4F, -5, 0}, Rv{-0.4F, 5, 0}, Rv{-0.4F, 5, 3}}});
    const auto mesh = meshOf(triangles);
    MeshScratch scratch;
    std::vector<Contact> out;
    const Sphere sphere{Vec3{-0.3F, 0, 1}, 0.5F};
    const Box box{Vec3{-0.8F, -0.5F, 0.5F}, Vec3{0.2F, 0.5F, 1.5F}};
    CHECK(coney::physics::overlapSphereMesh(*mesh, box, sphere, 0, 0, 0, 4, scratch, out) == 1);
    REQUIRE(out.size() == 2);
    CHECK(out[0].fraction == Approx(0.4F).margin(kTolerance)); // the deepest of those kept first
    CHECK(out[1].fraction == Approx(0.2F).margin(kTolerance));
    // A one-sided wall from behind is still found.
    out.clear();
    const Sphere behind{Vec3{0.2F, 0, 1}, 0.5F};
    CHECK(coney::physics::overlapSphereMesh(*meshOf(wallFacingMinusX(0.0F)), box, behind, 0, 0, 0, 4, scratch, out) ==
          1);
    CHECK(out[0].fraction == Approx(0.3F).margin(kTolerance));
    CHECK(coney::physics::overlapNothing() == 0);
}

TEST_CASE("the airborne push-out: the nearest wall, horizontally, r - distance", "[physics][mesh_sweep]") {
    MeshScratch scratch;
    const auto wall = meshOf(wallFacingMinusX(0.0F));
    const auto push = coney::physics::pushOutOfWalls(*wall, Sphere{Vec3{-0.3F, 0, 1}, 0.5F}, scratch);
    REQUIRE(push.has_value());
    checkVec(push.value_or(Vec3{}), Vec3{-0.2F, 0, 0});
    // Behind a one-sided wall: nothing.
    CHECK_FALSE(coney::physics::pushOutOfWalls(*wall, Sphere{Vec3{0.3F, 0, 1}, 0.5F}, scratch).has_value());
    // Two walls at 0.3 and 0.2: only the nearer pushes, by 0.3.
    auto both = wallFacingMinusX(0.0F);
    both.push_back(BuildTriangle{.corners = {Rv{-0.5F, -5, 0}, Rv{-0.5F, 5, 0}, Rv{-0.5F, 5, 3}}});
    const auto two = coney::physics::pushOutOfWalls(*meshOf(both), Sphere{Vec3{-0.3F, 0, 1}, 0.5F}, scratch);
    REQUIRE(two.has_value());
    checkVec(two.value_or(Vec3{}), Vec3{0.3F, 0, 0});
    // A wall leaning so that n.z = 0.6: the push is (n.x, n.y, 0) × (r − distance), shorter than r − distance.
    const float lean = 0.6F / 0.8F;
    const auto sloped = meshOf({BuildTriangle{.corners = {Rv{0, -5, 0}, Rv{3 * lean, 5, 3}, Rv{0, 5, 0}}},
                                BuildTriangle{.corners = {Rv{0, -5, 0}, Rv{3 * lean, -5, 3}, Rv{3 * lean, 5, 3}}}});
    // The centre 0.3 from the plane, over the face.
    const Vec3 centre{(1.0F * lean) - (0.3F * 0.8F), 0, 1.0F + (0.3F * 0.6F)};
    const auto slopedPush = coney::physics::pushOutOfWalls(*sloped, Sphere{centre, 0.5F}, scratch);
    REQUIRE(slopedPush.has_value());
    checkVec(slopedPush.value_or(Vec3{}), Vec3{-0.8F * 0.2F, 0, 0});
}
