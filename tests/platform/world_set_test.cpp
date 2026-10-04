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
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/texture_lookup.h"
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

// One part atomic: a four-vertex strip, two triangles, positions scaled by 0.5.
Bytes stripAtomic() {
    coney::test::AtomicFields fields;
    fields.meshChains.push_back(
        coney::test::ps2MeshChain({{coney::test::ps2Vertex(0, 0, 0), coney::test::ps2Vertex(4, 0, 0),
                                    coney::test::ps2Vertex(0, 4, 0), coney::test::ps2Vertex(4, 4, 0)}},
                                  true));
    fields.meshCounts.push_back(4);
    fields.triangles = 2;
    return coney::test::nativeAtomic(fields);
}

// A synthetic disc holding one streamed world, `test`: two sectors side by side (x 0..10 and 10..20), each the only
// sector of its part, with the manifest and both part files.
std::vector<std::byte> buildWorldDisc() {
    std::vector<Bytes> leaves{coney::test::worldSector({0, 0, 0}, {10, 10, 10}, 0, 1, {5, 5, 5}),
                              coney::test::worldSector({10, 0, 0}, {20, 10, 10}, 1, 2, {15, 5, 5})};
    const Bytes stream = coney::test::worldStream(2, leaves, 1, 2);
    const Bytes part1 = coney::test::partFile(0, {{0, stripAtomic()}});
    const Bytes part2 = coney::test::partFile(0, {{1, stripAtomic()}});
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

} // namespace

TEST_CASE("a name stands for an s and d pair or a single world", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    CHECK(coney::platform::worldNamesFor(wad, "test") == std::vector<std::string>{"test"});
    CHECK(coney::platform::worldNamesFor(wad, "nothing").error().code == coney::ErrorCode::NotFound);
}

TEST_CASE("a world loads resident, its parts come and go and the budget follows", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(10'000);
    {
        const std::vector<std::string> names{"test"};
        auto set = WorldSet::load(wad, names, budget, false);
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

TEST_CASE("a world whose heap does not fit is not loaded", "[world_set]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    TempDir dir;
    const coney::io::Wad wad = openWorldDisc(dir);
    coney::world::SectorBudget budget(500);
    const std::vector<std::string> names{"test"};
    CHECK_FALSE(WorldSet::load(wad, names, budget, false).has_value());
    CHECK(budget.used() == 0);
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
