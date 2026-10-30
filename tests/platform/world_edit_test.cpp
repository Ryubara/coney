// SPDX-License-Identifier: GPL-3.0-or-later
// The debug tools' world edits on a synthetic streamed world (docs/guides/running-viewers.md#world-edits): the world
// viewer reading every part for the spread view and placing each sector on its grid, a cut taking a piece and its
// collision out and moving both, a roof built from up-facing faces, and a hidden world. No game, no disc, no window
// (librw's NULL backend).
#include "platform/world_edit.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "core/game_timer.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "platform/render_engine.h"
#include "platform/world_set.h"
#include "platform/world_viewer_mode.h"
#include "raycast/collision_builder.h"
#include "support/fixtures.h"
#include "support/optional_value.h"
#include "support/world_fixtures.h"
#include "world/sector_budget.h"
#include "world/world_streamer.h"

using Catch::Approx;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WorldEditor;
using coney::platform::WorldSet;
using coney::test::Bytes;
using coney::test::TempDir;

namespace {

// One part atomic: a four-vertex strip, two triangles, lying flat in RenderWare's x-z plane (the game's ground):
// positions (0, 0, 0) to (4, 0, 4) scaled by 0.5, so 2 m square.
Bytes floorAtomic() {
    coney::test::AtomicFields fields;
    fields.meshChains.push_back(
        coney::test::ps2MeshChain({{coney::test::ps2Vertex(0, 0, 0), coney::test::ps2Vertex(4, 0, 0),
                                    coney::test::ps2Vertex(0, 0, 4), coney::test::ps2Vertex(4, 0, 4)}},
                                  true));
    fields.meshCounts.push_back(4);
    fields.triangles = 2;
    return coney::test::nativeAtomic(fields);
}

// A synthetic disc holding one streamed world, `test`: two sectors side by side (RenderWare x 0..10 and 10..20), each
// the only sector of its part, its atomic placed at (5, 5, 5) and (15, 5, 5); in the game's axes the floors lie at
// z 5, x 5-7 and 15-17, y -5 to -7.
std::vector<std::byte> buildWorldDisc() {
    std::vector<Bytes> leaves{coney::test::worldSector({0, 0, 0}, {10, 10, 10}, 0, 1, {5, 5, 5}),
                              coney::test::worldSector({10, 0, 0}, {20, 10, 10}, 1, 2, {15, 5, 5})};
    const Bytes stream = coney::test::worldStream(2, leaves, 1, 2);
    const Bytes part1 = coney::test::partFile(0, {{0, floorAtomic()}});
    const Bytes part2 = coney::test::partFile(0, {{1, floorAtomic()}});
    Bytes manifest;
    manifest.u32(static_cast<std::uint32_t>(stream.size())).u32(1000);
    manifest.u32(2).u32(static_cast<std::uint32_t>(part1.size())).u32(300);
    manifest.u32(static_cast<std::uint32_t>(part2.size())).u32(400);
    Bytes wad;
    Bytes dir;
    dir.header(4, 0, 0, 0);
    for (const auto& [name, data] :
         {std::pair<const char*, const Bytes*>{"test_sec.mem", &manifest}, std::pair{"test_sec.wld", &stream},
          std::pair{"test_ms1.sec", &part1}, std::pair{"test_ms2.sec", &part2}}) {
        dir.u32(static_cast<std::uint32_t>(wad.size()))
            .u32(static_cast<std::uint32_t>(data->size()))
            .u32(coney::nameHash(std::string("./ee_files/") + name));
        wad.append(data->span());
        wad.padTo((wad.size() + 2047) / 2048 * 2048);
    }
    return coney::test::buildIso({{"WARRIORS.DIR;1", dir.data()}, {"WARRIORS.WAD;1", wad.data()}});
}

// Writes the synthetic disc into `dir` and opens its WAD.
coney::io::Wad openWorldDisc(const TempDir& dir) {
    const auto image = dir.write("disc.iso", buildWorldDisc());
    auto disc = coney::io::Disc::open(image);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return std::move(*wad);
}

// The edits of `text`, which must read.
coney::debug::WorldEdits editsOf(const char* text) {
    auto edits = coney::debug::parseWorldEdits(text);
    REQUIRE(edits.has_value());
    return std::move(*edits);
}

// A collision mesh of one triangle on the first floor (z 5) and one far away, as the level's own.
std::unique_ptr<coney::raycast::CollisionMesh> levelMesh() {
    std::vector<coney::raycast::BuildTriangle> tris(2);
    tris[0].corners = {coney::raycast::Vec3{5, -7, 5}, coney::raycast::Vec3{7, -7, 5}, coney::raycast::Vec3{7, -5, 5}};
    tris[1].corners = {coney::raycast::Vec3{50, 50, 0}, coney::raycast::Vec3{52, 50, 0},
                       coney::raycast::Vec3{52, 52, 0}};
    auto mesh = coney::raycast::buildCollisionMesh(tris, 4.0F);
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

// Whether a ray straight down from `top` meets the mesh, and where.
std::optional<float> groundBelow(const coney::raycast::CollisionMesh& mesh, coney::raycast::Vec3 top) {
    const auto hit = mesh.rayCast(coney::raycast::Ray{top, {0, 0, -1}, 50.0F}, {}, 0);
    return hit ? std::optional<float>(top.z - hit->t) : std::nullopt;
}

} // namespace

TEST_CASE("the spread view reads every part and puts each sector on its grid", "[world_edit]") {
    // Expected: unverified, #1123 (Coney's own debug tool)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(10'000);
    auto mode = coney::platform::WorldViewerMode::create(**engine, wad, "test", budget, [](std::string_view) {});
    REQUIRE(mode.has_value());
    auto editor = std::make_shared<WorldEditor>(editsOf("spread columns=2 gap=1 scale=0.5 at=100,0,0\n"
                                                        "camera at=100,-30,10 heading=0 pitch=-20\n"),
                                                [](std::string_view) {});
    CHECK(editor->drawsEverySector());
    CHECK_FALSE(editor->drawsBackground());
    const auto camera = editor->viewerCamera();
    // Read through a pointer, which the optional-access check follows past a failed REQUIRE.
    const coney::world::DebugCamera* const viewer = camera ? &*camera : nullptr;
    REQUIRE(viewer != nullptr);
    (*mode)->placeCamera(*viewer);
    (*mode)->setEditHook(editor);
    coney::GameModeStack stack;
    stack.push(**mode);
    coney::GameTimer timer;
    CHECK(stack.runUntilEmpty(timer, {}, 2) == 2);

    // Both parts read before the first frame, though the camera is far from either.
    CHECK((*mode)->stats().preloaded == 2);
    // The camera stands where the statement says, in RenderWare's axes: game (100, -30, 10) is (100, 10, 30).
    CHECK((*mode)->camera().position().x == Approx(100.0F));
    CHECK((*mode)->camera().position().z == Approx(30.0F));
    // Each sector box (10 m square) is a cell of 5 + 1 m: sector 0's centre goes to game (100, 0), sector 1's to
    // (106, 0), both halved. The box's centre is RenderWare (5, 5, 5): the atomic's frame scales by 0.5.
    const rw::Atomic* first = (*mode)->worlds().loadedAtomic(0, 0);
    const rw::Atomic* second = (*mode)->worlds().loadedAtomic(0, 1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    rw::Matrix* m0 = const_cast<rw::Atomic*>(first)->getFrame()->getLTM();
    rw::Matrix* m1 = const_cast<rw::Atomic*>(second)->getFrame()->getLTM();
    CHECK(m0->right.x == Approx(0.5F));
    // A point at the box's centre, RenderWare (5, 5, 5) = local (0, 0, 0) from the origin (5, 5, 5), lands at the
    // cell's centre, game (100, 0, 2.5): RenderWare (100, 2.5, 0).
    CHECK(m0->pos.x == Approx(100.0F));
    CHECK(m0->pos.y == Approx(2.5F));
    CHECK(m0->pos.z == Approx(0.0F));
    CHECK(m1->pos.x == Approx(106.0F));
    // The render's own sector pass draws none of them (their boxes are where they were); the editor draws them all.
    CHECK((*mode)->worlds().atomic(0, 0) == nullptr);
    CHECK(editor->summary().find("2 sectors spread") != std::string::npos);
}

TEST_CASE("a cut takes a piece and its collision out of the world and moves both", "[world_edit]") {
    // Expected: unverified, #1123 (Coney's own debug tool; the collision box rule is CollisionMesh_SetEnabledInBox's,
    // research/collision.md#enable, confirmed (code))
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"test"};
    auto set = WorldSet::load(wad, names, budget, false);
    REQUIRE(set.has_value());
    auto mesh = levelMesh();
    std::string output;
    WorldEditor editor(editsOf("cut floor box=4,-8,4,8,-4,6\n"
                               "move floor step=10 offset=0,0,0\n"
                               "move floor step=20 offset=0,0,3\n"),
                       [&output](std::string_view text) { output += text; });

    // Nothing is cut while the sector under the box is not loaded.
    editor.afterStep(0, **set, mesh.get());
    CHECK(output.empty());
    REQUIRE(coney::world::requestPart(*(*set)->worlds()[0], 0, 1, budget, **set, 0).has_value());
    REQUIRE(coney::world::requestPart(*(*set)->worlds()[0], 0, 2, budget, **set, 0).has_value());
    editor.afterStep(1, **set, mesh.get());
    CAPTURE(output);
    CHECK(output.find("cut floor: 1 sectors under the box, 1 pieces, 2 triangles, 1 collision triangles") !=
          std::string::npos);
    // The sector keeps nothing of it; the other sector is untouched.
    CHECK((*set)->atomic(0, 0)->geometry->numTriangles == 0);
    CHECK((*set)->atomic(0, 1)->geometry->numTriangles == 2);
    // Its collision: the disc's triangle switched off, a copy in its place.
    CHECK((mesh->triangles()[0].flags & coney::raycast::kTriangleEnabled) == 0);
    CHECK(mesh->extraTriangleCount() == 1);
    CHECK(coney::test::got(groundBelow(*mesh, {6.5F, -6.5F, 20.0F})) == Approx(5.0F));

    // Halfway between the keys it has risen 1.5 m, and at the last key 3 m.
    editor.afterStep(15, **set, mesh.get());
    CHECK(coney::test::got(groundBelow(*mesh, {6.5F, -6.5F, 20.0F})) == Approx(6.5F));
    editor.afterStep(25, **set, mesh.get());
    CHECK(coney::test::got(groundBelow(*mesh, {6.5F, -6.5F, 20.0F})) == Approx(8.0F));
    CHECK(mesh->extraTriangleCount() == 1);
    CHECK(editor.summary().find("1 pieces cut (2 triangles), 1 collision triangles moved") != std::string::npos);
}

TEST_CASE("a roof gets collision from the up-facing faces under its box, and a hidden world is not drawn",
          "[world_edit]") {
    // Expected: unverified, #1123 (Coney's own debug tool)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"test"};
    auto set = WorldSet::load(wad, names, budget, false);
    REQUIRE(set.has_value());
    auto mesh = levelMesh();
    WorldEditor editor(editsOf("roof box=14,-8,4,18,-4,6 material=16\nhide s\n"), [](std::string_view) {});
    CHECK_FALSE(editor.drawsWorld(0));
    CHECK(editor.drawsBackground());
    CHECK(editor.drawsWorld(1));
    REQUIRE(coney::world::requestPart(*(*set)->worlds()[0], 0, 2, budget, **set, 0).has_value());
    // Nothing stands at the second floor before; its two faces after.
    CHECK_FALSE(groundBelow(*mesh, {16.0F, -6.0F, 20.0F}).has_value());
    editor.afterStep(0, **set, mesh.get());
    CHECK(mesh->extraTriangleCount() == 2);
    // The hidden `s` world gives the render no atomic; the `d` world is drawn as before.
    CHECK((*set)->atomic(0, 1) == nullptr);
    CHECK((*set)->loadedAtomic(0, 1) != nullptr);
    CHECK(coney::test::got(groundBelow(*mesh, {16.0F, -6.0F, 20.0F})) == Approx(5.0F));
    const auto hit = mesh->rayCast(coney::raycast::Ray{{16.0F, -6.0F, 20.0F}, {0, 0, -1}, 50.0F}, {}, 0);
    REQUIRE(hit.has_value());
    CHECK((hit && hit->material == 16));
    // The visible faces stay where they are.
    CHECK((*set)->loadedAtomic(0, 1)->geometry->numTriangles == 2);
}

TEST_CASE("a sector read again after its cut or its spread is cut and placed again", "[world_edit]") {
    // Expected: unverified, #1123 (Coney's own debug tool; the streamer frees and rereads parts as the player moves,
    // research/world.md#streaming)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"test"};
    auto set = WorldSet::load(wad, names, budget, false);
    REQUIRE(set.has_value());
    REQUIRE(coney::world::requestPart(*(*set)->worlds()[0], 0, 1, budget, **set, 0).has_value());
    REQUIRE(coney::world::requestPart(*(*set)->worlds()[0], 0, 2, budget, **set, 0).has_value());

    SECTION("a cut") {
        auto mesh = levelMesh();
        WorldEditor editor(editsOf("cut floor box=4,-8,4,8,-4,6\n"), [](std::string_view) {});
        editor.afterStep(0, **set, mesh.get());
        REQUIRE((*set)->atomic(0, 0)->geometry->numTriangles == 0);
        // The streamer frees the part and reads it again from the disc: whole, so the building would show twice.
        (*set)->unloadPart(0, 1);
        editor.afterStep(1, **set, mesh.get());
        REQUIRE((*set)->loadPart(0, 1).has_value());
        REQUIRE((*set)->atomic(0, 0)->geometry->numTriangles == 2);
        editor.afterStep(2, **set, mesh.get());
        // Cut again, with no second piece and the collision as it was.
        CHECK((*set)->atomic(0, 0)->geometry->numTriangles == 0);
        CHECK((*set)->atomic(0, 1)->geometry->numTriangles == 2);
        CHECK(mesh->extraTriangleCount() == 1);
        CHECK(editor.summary().find("1 pieces cut (2 triangles)") != std::string::npos);
    }

    SECTION("a spread") {
        WorldEditor editor(editsOf("spread columns=2 gap=1 scale=0.5 at=100,0,0\n"), [](std::string_view) {});
        editor.afterStep(0, **set, nullptr);
        (*set)->unloadPart(0, 2);
        editor.afterStep(1, **set, nullptr);
        REQUIRE((*set)->loadPart(0, 2).has_value());
        editor.afterStep(2, **set, nullptr);
        // Sector 1 is in its cell again (game (106, 0), RenderWare x 106), halved.
        rw::Matrix* m1 = (*set)->loadedAtomic(0, 1)->getFrame()->getLTM();
        CHECK(m1->right.x == Approx(0.5F));
        CHECK(m1->pos.x == Approx(106.0F));
        CHECK(editor.summary().find("2 sectors spread") != std::string::npos);
    }
}
