// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: Rembrandt (the player's character, loaded from the disc) walking over the
// default sandbox course (assets/sandbox/default.layout) on the sandbox's collision mesh, driven by the scripted pads
// in tests/support at partial stick deflections, on the fixed 30 Hz step with no window and no clock. The course is
// Coney's own; only the character comes from the disc. They run only when the environment variable CONEY_DISC names
// the disc and skip otherwise; they print positions and a hash of the run only, never data (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
#include "human/body.h"
#include "human/player.h"
#include "platform/render_engine.h"
#include "sandbox/sandbox_world.h"

using Catch::Approx;

namespace {

// What one frame of a scripted run left.
struct FrameRecord {
    coney::anim::Vec3 position;
    bool airborne = false;
};

// One scripted run: its frames, a hash of their positions and how often the player was put back at the start.
struct Run {
    std::vector<FrameRecord> frames;
    std::uint64_t hash = 0;
    std::uint32_t respawns = 0;
};

// The player's character from the disc CONEY_DISC names, and the default sandbox course.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct Course {
    std::optional<coney::io::Wad> wad;
    std::unique_ptr<coney::human::PlayerCharacter> character;
    std::unique_ptr<coney::sandbox::SandboxWorld> world;
};

// Loads Rembrandt from the disc at `discPath` and builds the default sandbox layout from the repository's assets.
Course loadCourse(const char* discPath) {
    Course course;
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    const coney::io::Wad& discWad = course.wad.emplace(std::move(*wad));
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    auto character = coney::human::PlayerCharacter::load(discWad, table, coney::human::kPlayerModel);
    REQUIRE(character.has_value());
    course.character = std::move(*character);
    auto world = coney::sandbox::SandboxWorld::load(std::filesystem::path(CONEY_ASSETS_DIR) / "sandbox",
                                                    coney::sandbox::kDefaultLayout);
    REQUIRE(world.has_value());
    REQUIRE(world->collision() != nullptr);
    course.world = std::make_unique<coney::sandbox::SandboxWorld>(std::move(*world));
    return course;
}

// FNV-1a over `bytes`, continuing from `hash`.
std::uint64_t fnv1a(std::uint64_t hash, const void* bytes, std::size_t size) {
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 0x100000001b3ULL;
    }
    return hash;
}

// Runs `frames` frames of the input script `name` (in tests/support) on a fresh player standing at `start` (game axes,
// on the ground) facing +y, and prints where he was every second.
Run runScript(const Course& course, const char* label, const std::string& name, coney::anim::Vec3 start,
              std::uint64_t frames) {
    const std::filesystem::path path = std::filesystem::path(CONEY_TEST_SUPPORT_DIR) / name;
    auto events = coney::loadInputScript(path.string());
    REQUIRE(events.has_value());
    coney::ScriptedInput input(std::move(*events));
    coney::Pads pads;
    const coney::raycast::CollisionMesh* mesh = course.world->collision();
    coney::human::Player player(*course.character, mesh,
                                coney::human::PlayerStart{.position = start, .headingDegrees = 0.0F});
    Run run;
    run.hash = 0xcbf29ce484222325ULL;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        pads.update(input.sample(frame));
        player.update(pads.port(0), mesh);
        const coney::human::Human& human = player.human();
        run.frames.push_back(FrameRecord{.position = human.position(), .airborne = human.airborne()});
        // The hash takes positions to the millimetre, so it names the run on every compiler.
        for (const float axis : {human.position().x, human.position().y, human.position().z}) {
            const auto millimetres = static_cast<std::int32_t>(std::lround(axis * 1000.0F));
            run.hash = fnv1a(run.hash, &millimetres, sizeof millimetres);
        }
        if ((frame + 1) % 30 == 0) {
            const coney::anim::Vec3 p = human.position();
            std::printf("sandbox %s: frame %3llu at (%.2f, %.2f, %.2f)%s\n", label,
                        static_cast<unsigned long long>(frame) + 1, p.x, p.y, p.z, human.airborne() ? " airborne" : "");
        }
    }
    run.respawns = player.respawns();
    std::printf("sandbox %s: %llu frames, hash %016llx, %u respawns\n", label, static_cast<unsigned long long>(frames),
                static_cast<unsigned long long>(run.hash), run.respawns);
    return run;
}

// The highest the player stood during `run`.
float highest(const Run& run) {
    float top = run.frames.front().position.z;
    for (const FrameRecord& record : run.frames) {
        top = std::max(top, record.position.z);
    }
    return top;
}

} // namespace

TEST_CASE("Rembrandt walks up a slope, a stair set and over low ledges to a wall in the default sandbox",
          "[disc][player][sandbox]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Course course = loadCourse(discPath);

    SECTION("the 20 degree ramp at x = -35 to the 2 m platform at its top") {
        const Run run = runScript(course, "slope", "sandbox_walk_forward.txt", {-35.0F, 6.0F, 0.0F}, 300);
        const FrameRecord& last = run.frames.back();
        CHECK(last.position.z == Approx(2.0F).margin(0.05));
        CHECK(last.position.x == Approx(-35.0F).margin(0.5));
        CHECK(last.position.y > 16.0F);
        CHECK(run.respawns == 0);
        // The same script again is the same run.
        CHECK(runScript(course, "slope again", "sandbox_walk_forward.txt", {-35.0F, 6.0F, 0.0F}, 300).hash == run.hash);
    }

    SECTION("the stair set of 20 cm rises at x = 8 to the 2 m platform at its top") {
        const Run run = runScript(course, "stairs", "sandbox_walk_short.txt", {8.0F, 6.0F, 0.0F}, 210);
        const FrameRecord& last = run.frames.back();
        CHECK(last.position.z == Approx(2.0F).margin(0.05));
        CHECK(last.position.x == Approx(8.0F).margin(0.5));
        CHECK(run.respawns == 0);
    }

    SECTION("the ledges at x = 32: over the 10 and 25 cm blocks, stopped by the 50 cm one") {
        const Run run = runScript(course, "ledge", "sandbox_walk_forward.txt", {32.0F, -14.5F, 0.0F}, 300);
        // Wall faces under 0.25 m do not stop the walking body and the ground snap lifts the feet on; the 25 cm
        // block's 3 m wide face is two slivers the same rule skips (docs/research/characters.md#walls).
        CHECK(highest(run) == Approx(0.25F).margin(0.02));
        // The 50 cm block is a wall: the walking sphere (0.485 m for Rembrandt) holds him short of its face at
        // y = -5.5.
        const FrameRecord& last = run.frames.back();
        CHECK(last.position.z == Approx(0.0F).margin(0.02));
        const float stop = -5.5F - coney::human::playerWalkingRadius(coney::human::kPlayerBodyScale);
        CHECK(last.position.y > stop - 0.03F);
        CHECK(last.position.y < stop + 0.03F);
        CHECK(!last.airborne);
        CHECK(run.respawns == 0);
    }
}
