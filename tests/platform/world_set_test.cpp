// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_set.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "graphics/asset_store.h"
#include "platform/object_draw.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "platform/texture_dictionary.h"
#include "platform/texture_lookup.h"
#include "platform/world_atomic.h"
#include "platform/world_viewer_mode.h"
#include "support/fixtures.h"
#include "support/rw_fixtures.h"
#include "support/world_fixtures.h"
#include "world/sector_budget.h"

using Catch::Approx;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WorldSet;
using coney::test::Bytes;
using coney::test::TempDir;

namespace {

// One part atomic: a four-vertex strip, two triangles, positions scaled by 0.5, its material textured by `texture`
// (none when empty).
Bytes stripAtomic(const std::string& texture = {}) {
    coney::test::AtomicFields fields;
    fields.texture = texture;
    fields.meshChains.push_back(
        coney::test::ps2MeshChain({{coney::test::ps2Vertex(0, 0, 0), coney::test::ps2Vertex(4, 0, 0),
                                    coney::test::ps2Vertex(0, 4, 0), coney::test::ps2Vertex(4, 4, 0)}},
                                  true));
    fields.meshCounts.push_back(4);
    fields.triangles = 2;
    return coney::test::nativeAtomic(fields);
}

// A synthetic disc holding one streamed world, `test`: two sectors side by side (x 0..10 and 10..20), each the only
// sector of its part, with the manifest and both part files (`part1` and `part2`, untextured strips when empty), and
// the files of `extra` (name and contents) after them.
std::vector<std::byte> buildWorldDisc(Bytes part1 = {}, Bytes part2 = {},
                                      const std::vector<std::pair<std::string, Bytes>>& extra = {}) {
    std::vector<Bytes> leaves{coney::test::worldSector({0, 0, 0}, {10, 10, 10}, 0, 1, {5, 5, 5}),
                              coney::test::worldSector({10, 0, 0}, {20, 10, 10}, 1, 2, {15, 5, 5})};
    const Bytes stream = coney::test::worldStream(2, leaves, 1, 2);
    if (part1.size() == 0) {
        part1 = coney::test::partFile(0, {{0, stripAtomic()}});
    }
    if (part2.size() == 0) {
        part2 = coney::test::partFile(0, {{1, stripAtomic()}});
    }
    Bytes manifest;
    manifest.u32(static_cast<std::uint32_t>(stream.size())).u32(1000);
    manifest.u32(2).u32(static_cast<std::uint32_t>(part1.size())).u32(300);
    manifest.u32(static_cast<std::uint32_t>(part2.size())).u32(400);

    std::vector<std::pair<std::string, const Bytes*>> files{
        {"test_sec.mem", &manifest}, {"test_sec.wld", &stream}, {"test_ms1.sec", &part1}, {"test_ms2.sec", &part2}};
    for (const auto& [name, data] : extra) {
        files.emplace_back(name, &data);
    }
    Bytes wad;
    Bytes dir;
    dir.header(static_cast<std::uint32_t>(files.size()), 0, 0, 0);
    for (const auto& [name, data] : files) {
        dir.u32(static_cast<std::uint32_t>(wad.size()))
            .u32(static_cast<std::uint32_t>(data->size()))
            .u32(coney::nameHash(std::string("./ee_files/") + name));
        wad.append(data->span());
        wad.padTo((wad.size() + 2047) / 2048 * 2048);
    }
    return coney::test::buildIso({{"WARRIORS.DIR;1", dir.data()}, {"WARRIORS.WAD;1", wad.data()}});
}

// Writes the synthetic disc (with `part1`, `part2` and `extra` as buildWorldDisc() takes them) into `dir` and opens
// its WAD.
coney::io::Wad openWorldDisc(const TempDir& dir, Bytes part1 = {}, Bytes part2 = {},
                             const std::vector<std::pair<std::string, Bytes>>& extra = {}) {
    const auto image = dir.write("disc.iso", buildWorldDisc(std::move(part1), std::move(part2), extra));
    auto disc = coney::io::Disc::open(image);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return std::move(*wad);
}

} // namespace

TEST_CASE("a name stands for an s and d pair or a single world", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    CHECK(coney::platform::worldNamesFor(wad, "test") == std::vector<std::string>{"test"});
    CHECK(coney::platform::worldNamesFor(wad, "nothing").error().code == coney::ErrorCode::NotFound);
}

// Expected: world.md#loading-a-world, confirmed (code): the world is resident once loaded and its parts take room
// from the budget while loaded, on the synthetic two-part fixture.
TEST_CASE("a world loads resident, its parts come and go and the budget follows", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(10'000);
    {
        const std::vector<std::string> names{"test"};
        auto set = WorldSet::load(wad, names, budget, false, (*engine)->sceneAssets());
        REQUIRE(set.has_value());
        CHECK(budget.used() == 1000); // the manifest's world heap
        REQUIRE((*set)->worlds().size() == 1);
        coney::world::StreamedWorld& world = *(*set)->worlds()[0];
        CHECK(world.partCount() == 2);
        CHECK((*set)->atomic(0, 0) == nullptr);

        // Part 2 through the streamer's request: room for 400 bytes, then its atomic in sector 1.
        auto requested = coney::world::requestPart(world, 0, 2, budget, **set, 100);
        REQUIRE(requested.has_value());
        CHECK(*requested);
        CHECK(budget.used() == 1400);
        rw::Atomic* atomic = (*set)->atomic(0, 1);
        REQUIRE(atomic != nullptr);
        CHECK(atomic->getFrame()->getLTM()->pos.x == 15.0F); // placed at its sector's origin
        CHECK((atomic->geometry->flags & rw::Geometry::MODULATE) != 0);
        CHECK((atomic->geometry->flags & rw::Geometry::NATIVE) == 0);
        CHECK((*set)->residentAtomics() == 1);

        (*set)->unloadPart(0, 2);
        world.markPartUnloaded(2);
        budget.release(400);
        CHECK((*set)->atomic(0, 1) == nullptr);
        CHECK((*set)->residentAtomics() == 0);
        REQUIRE(coney::world::requestPart(world, 0, 1, budget, **set, 100).has_value());
    }
    CHECK(budget.used() == 0); // the set gave back its world and its loaded part
}

// Expected: world.md#loading-a-world, confirmed (code): a world whose heap finds no room is not loaded.
TEST_CASE("a world whose heap does not fit is not loaded", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(500);
    const std::vector<std::string> names{"test"};
    CHECK_FALSE(WorldSet::load(wad, names, budget, false, (*engine)->sceneAssets()).has_value());
    CHECK(budget.used() == 0);
}

// Expected: graphics.md#texture-lookup, confirmed (code): part 2's material names a texture only part 1's dictionary
// holds, so with part 1 loaded first the lookup finds it there, and the set counts that material as borrowing another
// part's texture (the count is Coney's own guard: docs/engine/renderer.md#texture-lifetime).
TEST_CASE("a material taking its texture from another part's dictionary is counted", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad =
        openWorldDisc(dir,
                      coney::test::partFile(0, {{0, stripAtomic("own")}},
                                            coney::test::texDictionary({coney::test::rgbaTexture2x2("own"),
                                                                        coney::test::rgbaTexture2x2("lent")})),
                      coney::test::partFile(0, {{1, stripAtomic("lent")}}));
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"test"};
    auto set = WorldSet::load(wad, names, budget, false, (*engine)->sceneAssets());
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];

    REQUIRE(coney::world::requestPart(world, 0, 1, budget, **set, 100).has_value());
    CHECK((*set)->borrowedTextures() == 0); // part 1's material takes its texture from its own dictionary
    REQUIRE(coney::world::requestPart(world, 0, 2, budget, **set, 100).has_value());
    REQUIRE((*set)->atomic(0, 1) != nullptr);
    REQUIRE((*set)->atomic(0, 1)->geometry->matList.materials[0]->texture != nullptr);
    CHECK((*set)->borrowedTextures() == 1);
}

// Expected: graphics.md#texture-lifetime, confirmed (code): RenderWare's texture destroy (0x00488d90) only drops a
// reference, and a dictionary's destroy (0x00488c58) calls it for each texture, so a texture a material still holds
// outlives the part dictionary it came from. An object's material finds its texture by name through every loaded
// dictionary (graphics.md#texture-lookup), here part 1's; after part 1 unloads, the object's mesh still names a
// texture the scene assets hold (docs/engine/renderer.md#texture-lifetime), until the mesh goes.
TEST_CASE("an object material taking a streamed part's texture keeps it after the part unloads", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::SceneAssets& assets = (*engine)->sceneAssets();
    REQUIRE(assets.converting());
    TempDir dir;
    const coney::io::Wad wad =
        openWorldDisc(dir, coney::test::partFile(0, {{0, stripAtomic("own")}},
                                                 coney::test::texDictionary({coney::test::rgbaTexture2x2("own"),
                                                                             coney::test::rgbaTexture2x2("lent")})));
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"test"};
    auto set = WorldSet::load(wad, names, budget, false, assets);
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    REQUIRE(coney::world::requestPart(world, 0, 1, budget, **set, 100).has_value());

    // The object's model is read while part 1 is loaded, its material naming part 1's texture; then its mesh goes in,
    // as PlacedObjects and ParkedCars put theirs (addObjectMesh()).
    const Bytes section = stripAtomic("lent");
    auto object = coney::platform::WorldAtomic::read(section.span(), {0, 0, 0});
    REQUIRE(object.has_value());
    object->unpack();
    REQUIRE(object->atomic()->geometry->matList.materials[0]->texture != nullptr);
    const coney::platform::ObjectMesh mesh = coney::platform::addObjectMesh(assets, *object->atomic());
    REQUIRE(mesh.mesh.valid());
    REQUIRE(assets.materialTextures(mesh.mesh).size() == 1);
    const coney::graphics::TextureId lent = assets.materialTextures(mesh.mesh)[0];
    REQUIRE(lent.valid());
    REQUIRE(assets.store().texture(lent) != nullptr);

    // Part 1 goes while the object stays: the texture its mesh names is still held.
    (*set)->unloadPart(0, 1);
    world.markPartUnloaded(1);
    CHECK(assets.store().texture(lent) != nullptr);
    CHECK(assets.materialTextures(mesh.mesh)[0] == lent);

    // The object goes too: nothing names the texture any more, so it is dropped.
    coney::platform::removeObjectMesh(assets, mesh);
    CHECK(assets.store().texture(lent) == nullptr);
}

// Expected: graphics.md#texture-lifetime, confirmed (code): as above, for a part's material that found its texture in
// another part's dictionary (the case borrowedTextures() counts): that part unloading leaves the texture held.
TEST_CASE("a part material taking another part's texture keeps it after that part unloads", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::SceneAssets& assets = (*engine)->sceneAssets();
    REQUIRE(assets.converting());
    TempDir dir;
    const coney::io::Wad wad =
        openWorldDisc(dir,
                      coney::test::partFile(0, {{0, stripAtomic("own")}},
                                            coney::test::texDictionary({coney::test::rgbaTexture2x2("own"),
                                                                        coney::test::rgbaTexture2x2("lent")})),
                      coney::test::partFile(0, {{1, stripAtomic("lent")}}));
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"test"};
    auto set = WorldSet::load(wad, names, budget, false, assets);
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    REQUIRE(coney::world::requestPart(world, 0, 1, budget, **set, 100).has_value());
    REQUIRE(coney::world::requestPart(world, 0, 2, budget, **set, 100).has_value());
    const WorldSet::SectorMesh* sector = (*set)->sectorMesh(0, 1);
    REQUIRE(sector != nullptr);
    REQUIRE(assets.materialTextures(sector->mesh).size() == 1);
    const coney::graphics::TextureId lent = assets.materialTextures(sector->mesh)[0];
    REQUIRE(lent.valid());

    (*set)->unloadPart(0, 1);
    world.markPartUnloaded(1);
    CHECK(assets.store().texture(lent) != nullptr);
}

// Expected: graphics.md#texture-lifetime, confirmed (code): as above; the debug tools' refresh of a sector's mesh
// (WorldSet::refreshMesh(), Coney's own) converts it again under a new id, and the new mesh still names the borrowed
// texture, though the old mesh was its last holder once the lending part unloaded.
TEST_CASE("a refreshed part mesh keeps a texture it took from a part since unloaded", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::SceneAssets& assets = (*engine)->sceneAssets();
    REQUIRE(assets.converting());
    TempDir dir;
    const coney::io::Wad wad =
        openWorldDisc(dir,
                      coney::test::partFile(0, {{0, stripAtomic("own")}},
                                            coney::test::texDictionary({coney::test::rgbaTexture2x2("own"),
                                                                        coney::test::rgbaTexture2x2("lent")})),
                      coney::test::partFile(0, {{1, stripAtomic("lent")}}));
    coney::world::SectorBudget budget(10'000);
    const std::vector<std::string> names{"test"};
    auto set = WorldSet::load(wad, names, budget, false, assets);
    REQUIRE(set.has_value());
    coney::world::StreamedWorld& world = *(*set)->worlds()[0];
    REQUIRE(coney::world::requestPart(world, 0, 1, budget, **set, 100).has_value());
    REQUIRE(coney::world::requestPart(world, 0, 2, budget, **set, 100).has_value());
    (*set)->unloadPart(0, 1);
    world.markPartUnloaded(1);

    (*set)->refreshMesh(0, 1);
    const WorldSet::SectorMesh* sector = (*set)->sectorMesh(0, 1);
    REQUIRE(sector != nullptr);
    REQUIRE(assets.materialTextures(sector->mesh).size() == 1);
    const coney::graphics::TextureId lent = assets.materialTextures(sector->mesh)[0];
    CHECK(lent.valid());
    CHECK(assets.store().texture(lent) != nullptr);
}

TEST_CASE("textures are found by name across every registered dictionary, newest first", "[texture_lookup]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::installGlobalTextureLookup();
    auto older = coney::platform::TextureDictionary::read(
        coney::test::texDictionary({coney::test::rgbaTexture2x2("shared"), coney::test::rgbaTexture2x2("old")}).span());
    auto newer = coney::platform::TextureDictionary::read(
        coney::test::texDictionary({coney::test::rgbaTexture2x2("shared")}).span());
    REQUIRE(older.has_value());
    REQUIRE(newer.has_value());
    CHECK(rw::Texture::read("old", nullptr) == nullptr); // nothing registered: no file is read, no stand-in made
    {
        const coney::platform::TextureLookupEntry first(older->rwDictionary());
        const coney::platform::TextureLookupEntry second(newer->rwDictionary());
        rw::Texture* shared = rw::Texture::read("SHARED", nullptr);
        REQUIRE(shared != nullptr);
        CHECK(shared == newer->textures().front()); // the newest dictionary is searched first, in any case
        shared->destroy();
        rw::Texture* old = rw::Texture::read("old", nullptr);
        CHECK(old == older->textures().back());
        old->destroy();
    }
    CHECK(rw::Texture::read("shared", nullptr) == nullptr); // taken out again
}

TEST_CASE("the world viewer preloads, streams and flies under a script in test mode", "[world_viewer]") {
    // Expected: world.md#streaming, confirmed (code)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(1400); // the world and one part, not both
    std::string output;
    auto mode = coney::platform::WorldViewerMode::create(**engine, wad, "test", budget,
                                                         [&output](std::string_view text) { output += text; });
    REQUIRE(mode.has_value());
    // The start is above part 1's sector.
    CHECK((*mode)->camera().position().x == 5.0F);
    CHECK((*mode)->camera().position().y == 10.0F);

    // Strafe left for one second: looking along +z the screen's left is +x, towards part 2.
    auto script = coney::parseInputScript("0 stick left -100 0\n30 stick left 0 0\n");
    REQUIRE(script.has_value());
    coney::ScriptedInput input(std::move(*script));
    coney::GameModeStack stack;
    stack.setInput(&input);
    stack.push(**mode);
    coney::GameTimer timer;
    CHECK(stack.runUntilEmpty(timer, {}, 45) == 45);

    CAPTURE(output);
    CHECK((*mode)->stats().preloaded == 1); // part 1; part 2 does not fit beside it
    CHECK((*mode)->camera().position().x == Approx(5.0F + coney::world::DebugCamera::kSpeed));
    CHECK((*mode)->stats().unloads == 1); // over part 2's sector part 1 is farthest: it goes
    CHECK((*mode)->stats().loads == 1);   // and part 2 comes in
    CHECK(budget.used() <= budget.capacity());
    CHECK(output.find("freed test part 1") != std::string::npos);
    CHECK(output.find("read test part 2") != std::string::npos);
}

// The pools a running game holds before a level's worlds, the Global Data Pool for warriors.glr (101 % of its 1000
// bytes) and the World Level Pool, go back to the budget with the scenery, as WorldManager_Unload gives the level's
// back (docs/research/physics-world.md#u18-coney): a second level then streams with the room the first had.
TEST_CASE("a level's scenery gives back the pools it reserved when it goes", "[world_viewer]") {
    // Expected: physics-world.md#u18-construct, confirmed (code)
    // Expected: physics-world.md#u18-coney, confirmed (code)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    Bytes glr;
    glr.fill(1000, 0);
    const coney::io::Wad wad = openWorldDisc(dir, {}, {}, {{"warriors.glr", glr}});
    coney::world::SectorBudget budget(10'000);
    for (int load = 0; load < 2; ++load) {
        auto scenery = coney::platform::loadLevelScenery(**engine, wad, "test", budget, [](std::string_view) {});
        REQUIRE(scenery.has_value());
        CHECK(budget.used() == 1000 + 1010); // the world's heap and the global data
    }
    CHECK(budget.used() == 0);
}

// A level whose level file does not read gives back what loadLevelScenery() reserved before it: the global data
// (1010 bytes) and the World Level Pool (its 256 KB minimum for a 16-byte file).
TEST_CASE("a level's scenery that fails to load gives back the pools it reserved", "[world_viewer]") {
    // Expected: physics-world.md#u18-load, confirmed (code)
    // Expected: physics-world.md#u18-coney, confirmed (code)
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    Bytes glr;
    glr.fill(1000, 0);
    Bytes lev;
    lev.fill(16, 0xFF);
    const coney::io::Wad wad = openWorldDisc(dir, {}, {}, {{"warriors.glr", glr}, {"test.lev", lev}});
    coney::world::SectorBudget budget(1'000'000);
    CHECK_FALSE(coney::platform::loadLevelScenery(**engine, wad, "test", budget, [](std::string_view) {}).has_value());
    CHECK(budget.used() == 0);
}
