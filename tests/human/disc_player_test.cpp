// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: Rembrandt in level99, made as the level script makes him and driven by the
// scripted pads in tests/support (partial stick deflections, arcs, stop-and-go, running into the scenery), on the
// fixed 30 Hz step with no window and no clock. They run only when the environment variable CONEY_DISC names the
// disc and skip otherwise; they print counts and speeds only, never data (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/character_data.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "human/player.h"
#include "human/player_trace.h"
#include "platform/level_file.h"
#include "platform/render_engine.h"

using Catch::Approx;

namespace {

// What one frame of a scripted run left.
struct FrameRecord {
    coney::anim::Vec3 position;
    float speed = 0.0F;
    std::uint32_t animId = 0;
    bool airborne = false;
    float cameraDistance = 0.0F;
};

// The disc, the level's collision and the player's character, loaded once per test. Its implicit members can only
// throw on a failed allocation, which ends the test run either way.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct Level99 {
    std::optional<coney::io::Wad> wad;
    std::unique_ptr<coney::world::LevelObject> level;
    std::unique_ptr<coney::human::PlayerCharacter> character;
};

// Loads level99 and Rembrandt from the disc CONEY_DISC names; null members when it is not set.
Level99 loadLevel99(const char* discPath) {
    Level99 loaded;
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    loaded.wad.emplace(std::move(*wad));
    auto level = coney::platform::loadLevel(*loaded.wad, "level99", false);
    REQUIRE(level.has_value());
    loaded.level = std::move(*level);
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    auto character = coney::human::PlayerCharacter::load(*loaded.wad, table, coney::human::kPlayerModel);
    REQUIRE(character.has_value());
    loaded.character = std::move(*character);
    return loaded;
}

// Runs `frames` frames of the input script `name` (in tests/support) on a fresh player at level99's start.
std::vector<FrameRecord> runScript(const Level99& loaded, const std::string& name, std::uint64_t frames) {
    const std::filesystem::path path = std::filesystem::path(CONEY_TEST_SUPPORT_DIR) / name;
    auto events = coney::loadInputScript(path.string());
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    coney::Pads pads;
    const coney::raycast::CollisionMesh* mesh = loaded.level->collision.get();
    const auto start = coney::human::researchedPlayerStart("level99");
    REQUIRE(start.has_value());
    // value_or keeps the access checked for clang-tidy, which does not know REQUIRE stops the test.
    coney::human::Player player(*loaded.character, mesh, start.value_or(coney::human::PlayerStart{}));
    std::vector<FrameRecord> records;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        pads.update(input.sample(frame));
        player.update(pads.port(0), mesh);
        const coney::human::Human& human = player.human();
        records.push_back(
            FrameRecord{.position = human.position(),
                        .speed = human.speed(),
                        .animId = human.animator().animId(),
                        .airborne = human.airborne(),
                        .cameraDistance = coney::anim::distance(player.camera().position(), player.camera().lookAt())});
    }
    CHECK(player.respawns() == 0);
    return records;
}

} // namespace

TEST_CASE("Rembrandt stands at level99's start, walks, runs, turns and stops under a scripted pad", "[disc][player]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Level99 loaded = loadLevel99(discPath);

    // His speeds come from the clips: the runtime values of characters.md.
    const coney::human::Speeds speeds =
        coney::human::speedsOf(loaded.character->anims(), coney::human::AnimSlots::player());
    CHECK(speeds.walk == Approx(1.629F).margin(1e-3));
    CHECK(speeds.jog == Approx(4.857F).margin(1e-3));
    CHECK(speeds.run == Approx(7.801F).margin(1e-3));
    CHECK(speeds.sprint == Approx(10.245F).margin(1e-3));

    const std::vector<FrameRecord> run = runScript(loaded, "play_walk.txt", 390);
    // Created 0.01 above the ground, then on it at the height the game showed (0.25), standing in the idle.
    CHECK(run[0].position.z == Approx(0.25F).margin(0.02));
    CHECK(run[29].animId == 388U);
    CHECK(run[29].speed == 0.0F);
    // Stick 0.3 from frame 30: the walk start, its own root motion times the body scale (0.762 m/s at runtime), then
    // the walk. The update the start clip begins does not move him.
    CHECK(run[30].animId == 413U);
    CHECK(run[30].speed == 0.0F);
    CHECK(run[35].speed == Approx(0.762F).margin(0.01));
    CHECK(run[60].animId == 408U);
    CHECK(run[60].speed == Approx(speeds.walk).margin(1e-3));
    // Stick 0.6 from frame 90: still the same walk.
    CHECK(run[120].speed == Approx(speeds.walk).margin(1e-3));
    // Full stick from frame 150: 0.8 m/s more per update up to the run speed.
    CHECK(run[151].speed - run[150].speed == Approx(0.8F).margin(1e-3));
    CHECK(run[165].speed == Approx(speeds.run).margin(1e-3));
    CHECK(run[165].animId == 410U);
    // Let go at frame 240 at a steady run: the speed measured there is a rounding short of the run speed, so he does
    // not skid (as in about 9 releases in 10 at runtime, docs/research/characters.md#run-stop) and stops at once in
    // the idle.
    CHECK(run[240].animId == 388U);
    CHECK(run[240].speed == 0.0F);
    CHECK(run[268].animId == 388U);
    CHECK(run[268].speed == 0.0F);
    // A 45 % stick to the left from frame 270 walks again, with the walk start first.
    CHECK(run[271].animId == 413U);
    CHECK(run[300].speed == Approx(speeds.walk).margin(1e-3));
    // Never in the air on this ground, the camera never further than the hard band's 5.65 m.
    float travelled = 0.0F;
    for (std::size_t i = 0; i < run.size(); ++i) {
        CHECK(!run[i].airborne);
        CHECK(run[i].cameraDistance <= 5.65F + 1e-3F);
        if (i > 0) {
            travelled +=
                std::hypot(run[i].position.x - run[i - 1].position.x, run[i].position.y - run[i - 1].position.y);
        }
    }
    CHECK(travelled > 15.0F);
    std::printf("level99 scripted walk: %zu frames, %.2f m travelled, final speed %.2f m/s\n", run.size(),
                static_cast<double>(travelled), static_cast<double>(run.back().speed));

    // The same script gives the same path every time.
    const std::vector<FrameRecord> again = runScript(loaded, "play_walk.txt", 390);
    CHECK(again.back().position == run.back().position);
}

TEST_CASE("the trace writes one line per step with every column its header names", "[disc][player]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Level99 loaded = loadLevel99(discPath);
    const coney::raycast::CollisionMesh* mesh = loaded.level->collision.get();
    const auto start = coney::human::researchedPlayerStart("level99");
    coney::human::Player player(*loaded.character, mesh, start.value_or(coney::human::PlayerStart{}));
    coney::Pads pads;
    pads.update(coney::PortSamples{});
    player.update(pads.port(0), mesh);
    const std::string_view header = coney::human::traceHeader();
    const std::string line = coney::human::traceLine(1, player);
    // As many commas in each, one newline at the end, the step first and the idle's clip in it.
    CHECK(std::ranges::count(header, ',') == std::ranges::count(line, ','));
    CHECK(std::ranges::count(header, ',') == 33);
    CHECK(header.back() == '\n');
    CHECK(line.back() == '\n');
    CHECK(line.starts_with("1,"));
    CHECK(line.find(",388,") != std::string::npos);
    std::printf("trace line: %zu bytes\n", line.size());
}

TEST_CASE("running into level99's scenery stops Rembrandt without letting him through", "[disc][player]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Level99 loaded = loadLevel99(discPath);

    const std::vector<FrameRecord> run = runScript(loaded, "play_wall.txt", 300);
    std::uint32_t blocked = 0;
    for (const FrameRecord& record : run) {
        CHECK(!record.airborne);
        CHECK(record.position.z > -1.0F); // still on the street, not under it
        blocked += record.speed == 0.0F ? 1U : 0U;
    }
    std::printf("level99 run into the scenery: %zu frames, %u at a standstill\n", run.size(), blocked);
}
