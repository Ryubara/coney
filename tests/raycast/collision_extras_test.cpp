// SPDX-License-Identifier: GPL-3.0-or-later

// The debug menus' Spawner in a level (docs/guides/debug-menu.md): the triangles of spawned sandbox primitives added
// to a level's collision mesh after the file's own, met by the same queries the player, the AI and the cameras use, and
// cleared again leaving the mesh as it was built. Synthetic meshes only (support/collision_fixtures.h).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "human/body.h"
#include "physics/contact.h"
#include "physics/mesh_sweep.h"
#include "raycast/collision_builder.h"
#include "raycast/collision_mesh.h"
#include "sandbox/sandbox_layout.h"
#include "sandbox/sandbox_mesh.h"
#include "support/collision_fixtures.h"

using Catch::Approx;
using coney::raycast::BuildTriangle;
using coney::raycast::CollisionMesh;
using coney::raycast::Ray;
using coney::raycast::Vec3;

namespace {

// The triangles of one spawned sandbox primitive: a box `size` with its base at `base`, turned `yaw` degrees, as the
// Spawner places one in front of the player.
std::vector<BuildTriangle> spawnedBox(coney::anim::Vec3 base, coney::anim::Vec3 size, float yaw = 0.0F) {
    coney::sandbox::SandboxLayout layout;
    coney::sandbox::Primitive box;
    box.shape = coney::sandbox::Shape::Box;
    box.base = base;
    box.size = size;
    box.yawDegrees = yaw;
    layout.primitives.push_back(box);
    return coney::sandbox::sandboxCollisionTriangles(layout);
}

// Every cell's list, in order: what the queries walk.
std::vector<std::vector<std::uint16_t>> allLists(const CollisionMesh& mesh) {
    std::vector<std::vector<std::uint16_t>> lists;
    const auto [nx, ny, nz] = mesh.cells();
    for (std::uint32_t z = 0; z < nz; ++z) {
        for (std::uint32_t y = 0; y < ny; ++y) {
            for (std::uint32_t x = 0; x < nx; ++x) {
                const auto cell = mesh.cellTriangles(x, y, z);
                lists.emplace_back(cell.begin(), cell.end());
            }
        }
    }
    return lists;
}

// A ray along +y at height 1 from (x, y0): toward a wall facing -y.
Ray alongY(float x, float y0, float length) { return Ray{{x, y0, 1.0F}, {0.0F, 1.0F, 0.0F}, length}; }

} // namespace

TEST_CASE("a spawned box joins a level-like mesh: rays stop on it, the walking sweep meets it, and it clears",
          "[collision_builder][debug]") {
    // Expected: physics-bodies.md#u2-mapping-into-the-c0-contact, confirmed (code)
    // Expected: unverified, #457 (synthetic debug mesh insertion and removal contract)
    // A floor over the whole 80 m grid, as a level's street.
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    const std::size_t builtTriangles = mesh->triangles().size();
    const std::size_t builtVertices = mesh->vertices().size();
    const auto builtLists = allLists(*mesh);
    REQUIRE_FALSE(mesh->rayCast(alongY(40.0F, 30.0F, 20.0F), {}, 0).has_value());

    // A wall 4 m wide, 0.3 m deep and 2.5 m tall across x = 40, its near face at y = 43.85 (the Spawner's wall).
    const std::vector<BuildTriangle> wall = spawnedBox({40.0F, 44.0F, 0.0F}, {4.0F, 0.3F, 2.5F});
    REQUIRE(mesh->addExtraTriangles(wall).has_value());
    CHECK(mesh->extraTriangleCount() == wall.size());
    CHECK(mesh->triangles().size() == builtTriangles + wall.size());
    // The file's triangles keep their indices: the new ones come after them, enabled.
    for (std::size_t i = builtTriangles; i < mesh->triangles().size(); ++i) {
        CHECK((mesh->triangles()[i].flags & coney::raycast::kTriangleEnabled) != 0);
        CHECK(mesh->triangles()[i].material == coney::raycast::kMaterialConcrete);
    }

    // A horizontal ray stops on the near face; one beside the wall passes.
    const std::optional<coney::raycast::RayHit> hit = mesh->rayCast(alongY(40.5F, 30.0F, 20.0F), {}, 0);
    REQUIRE(hit.has_value());
    CHECK(hit.value_or(coney::raycast::RayHit{}).t == Approx(13.85F).margin(1e-3));
    CHECK(hit.value_or(coney::raycast::RayHit{}).normal.y == Approx(-1.0F));
    CHECK_FALSE(mesh->rayCast(alongY(45.0F, 30.0F, 20.0F), {}, 0).has_value());
    // A drop onto its top lands 2.5 m up.
    Vec3 above{40.0F, 44.0F, 10.0F};
    REQUIRE(coney::raycast::dropToGround(*mesh, 20.0F, above));
    CHECK(above.z == Approx(2.6F));

    // The walking capsule's sweep meets it, as it meets a level's own wall (the human's walking body moves through
    // physics::sweepBody, whose mesh sweep this is).
    const float radius = coney::human::walkingRadius(1.0F);
    coney::physics::MeshScratch scratch;
    const coney::physics::CapsuleSweep walking{.feet = coney::anim::Vec3{40.0F, 43.0F, 0.0F},
                                               .radius = radius,
                                               .move = coney::anim::Vec3{0.0F, 1.0F, 0.0F},
                                               .ownerAirborne = false,
                                               .footGap = coney::human::bodyTuning().footGap,
                                               .minWallHeight = coney::human::bodyTuning().minWallHeight};
    const coney::physics::Box query{coney::anim::Vec3{38.0F, 41.0F, 0.0F}, coney::anim::Vec3{42.0F, 46.0F, 2.0F}};
    std::vector<coney::physics::Contact> contacts;
    REQUIRE(coney::physics::sweepCapsuleMesh(*mesh, query, walking, scratch, contacts) > 0);
    REQUIRE_FALSE(contacts.empty());
    // The sphere's front reaches the face at y = 43.85 after 0.85 m less its radius.
    CHECK(contacts.front().fraction == Approx(0.85F - radius).margin(1e-4));
    CHECK(contacts.front().halves[1].normal.y == Approx(-1.0F));
    const coney::anim::Vec3 centre{40.0F, 43.85F - (walking.minWallHeight - walking.footGap), radius + walking.footGap};
    const auto sphere = mesh->spherePush(radius, Vec3{centre.x, centre.y, centre.z}, {});
    CHECK(sphere.touched);

    // Cleared, the mesh is as built: the same triangles, vertices and cell lists, and nothing in the way.
    mesh->clearExtraTriangles();
    CHECK(mesh->extraTriangleCount() == 0);
    CHECK(mesh->triangles().size() == builtTriangles);
    CHECK(mesh->vertices().size() == builtVertices);
    CHECK(allLists(*mesh) == builtLists);
    CHECK_FALSE(mesh->rayCast(alongY(40.5F, 30.0F, 20.0F), {}, 0).has_value());
    contacts.clear();
    CHECK(coney::physics::sweepCapsuleMesh(*mesh, query, walking, scratch, contacts) == 0);
}

TEST_CASE("spawned triangles leave the file's own hits and their order alone", "[collision_builder][debug]") {
    // Expected: unverified, #457 (synthetic debug mesh insertion preserves existing triangles and cell lists)
    // A level wall facing -y at y = 50, and a spawned crate in front of it at y = 45.
    const auto mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusY(50.0F, 0.0F, 80.0F, 0.0F, 3.0F)));
    const auto builtHit = mesh->rayCast(alongY(20.0F, 30.0F, 40.0F), {}, 0);
    REQUIRE(builtHit.has_value());
    REQUIRE(mesh->addExtraTriangles(spawnedBox({40.0F, 45.0F, 0.0F}, {1.0F, 1.0F, 1.0F})).has_value());
    // Away from the crate the level's wall is hit as before, the same triangle at the same distance.
    const auto beside = mesh->rayCast(alongY(20.0F, 30.0F, 40.0F), {}, 0);
    REQUIRE(beside.has_value());
    CHECK(beside.value_or(coney::raycast::RayHit{}).triangle == builtHit.value_or(coney::raycast::RayHit{}).triangle);
    CHECK(beside.value_or(coney::raycast::RayHit{}).t == builtHit.value_or(coney::raycast::RayHit{}).t);
    // Through the crate, the crate is nearer.
    const auto through = mesh->rayCast(alongY(40.0F, 30.0F, 40.0F), {}, 0);
    REQUIRE(through.has_value());
    CHECK(through.value_or(coney::raycast::RayHit{}).t == Approx(14.5F).margin(1e-3));
    // Each cell's list starts with what it listed before.
    mesh->clearExtraTriangles();
    const auto built = allLists(*mesh);
    REQUIRE(mesh->addExtraTriangles(spawnedBox({40.0F, 45.0F, 0.0F}, {1.0F, 1.0F, 1.0F})).has_value());
    const auto extended = allLists(*mesh);
    REQUIRE(extended.size() == built.size());
    for (std::size_t cell = 0; cell < built.size(); ++cell) {
        REQUIRE(extended[cell].size() >= built[cell].size());
        CHECK(std::vector<std::uint16_t>(extended[cell].begin(),
                                         extended[cell].begin() + static_cast<std::ptrdiff_t>(built[cell].size())) ==
              built[cell]);
    }
}

TEST_CASE("spawned triangles outside the grid's box are still met, through its edge cells",
          "[collision_builder][debug]") {
    // Expected: unverified, #457 (synthetic debug meshes clamp out-of-grid primitives to edge cells)
    // The fixtures' grid covers [0, 80]²; a crate at x = 90 lies past its edge, where the queries clamp too.
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    REQUIRE(mesh->addExtraTriangles(spawnedBox({90.0F, 40.0F, 0.0F}, {2.0F, 2.0F, 2.0F})).has_value());
    Vec3 above{90.0F, 40.0F, 10.0F};
    REQUIRE(coney::raycast::dropToGround(*mesh, 20.0F, above));
    CHECK(above.z == Approx(2.1F));
}

TEST_CASE("adding spawned triangles is refused, the mesh unchanged, for what the format cannot hold",
          "[collision_builder][debug]") {
    // Expected: unverified, #457 (synthetic debug mesh capacity and finite-coordinate validation)
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    const std::size_t built = mesh->triangles().size();
    // More triangles than u16 indices can name.
    const std::vector<BuildTriangle> tooMany(coney::raycast::kMaxCollisionTriangles,
                                             spawnedBox({40.0F, 40.0F, 0.0F}, {1.0F, 1.0F, 1.0F}).front());
    CHECK(mesh->addExtraTriangles(tooMany).error().code == coney::ErrorCode::InvalidArgument);
    CHECK(mesh->triangles().size() == built);
    // A corner that is not finite.
    std::vector<BuildTriangle> nan = spawnedBox({40.0F, 40.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    nan.back().corners[1].x = std::numeric_limits<float>::quiet_NaN();
    CHECK(mesh->addExtraTriangles(nan).error().code == coney::ErrorCode::InvalidArgument);
    CHECK(mesh->triangles().size() == built);
    CHECK(mesh->extraTriangleCount() == 0);
    // Nothing to add is fine and adds nothing.
    CHECK(mesh->addExtraTriangles({}).has_value());
    CHECK(mesh->triangles().size() == built);
}
