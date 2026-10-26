// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: Rembrandt in level99 moved mid-play by the scripts' Teleport and
// TeleportToFlag, which leave the follow camera alone (docs/research/teleports.md, docs/research/camera.md#update).
// They run only when the environment variable CONEY_DISC names the disc and skip otherwise; they print nothing of the
// data (LEGAL.md).

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "characters/character_data.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "human/player.h"
#include "platform/level_file.h"
#include "platform/render_engine.h"

namespace {

// The disc, level99's collision and the player's character. Its implicit members can only throw on a failed
// allocation, which ends the test run either way.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct Level99 {
    std::optional<coney::io::Wad> wad;
    std::unique_ptr<coney::world::LevelObject> level;
    std::unique_ptr<coney::human::PlayerCharacter> character;
};

// Loads level99 and Rembrandt from the disc at `discPath`.
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

// Player 1 at level99's start, stepped with the pad at rest.
struct Standing {
    coney::ScriptedInput input{std::vector<coney::InputEvent>{}};
    coney::Pads pads;
    const coney::raycast::CollisionMesh* mesh;
    coney::human::Player player;
    std::uint64_t frame = 0;

    Standing(const Level99& loaded, const coney::human::PlayerStart& start)
        : mesh(loaded.level->collision.get()), player(*loaded.character, mesh, start) {}
    // Steps `count` updates.
    void step(int count) {
        for (int i = 0; i < count; ++i) {
            pads.update(input.sample(frame++));
            player.update(pads.port(0), mesh);
        }
    }
};

} // namespace

TEST_CASE("a short Teleport mid-play does not cut the camera: the follow camera glides to the new place",
          "[disc][player]") {
    // Expected: teleports.md#motion-reset, confirmed (code) (Object_Teleport 0x00385bb8 and Object_TeleportToFlag
    // 0x00385db0 call no camera code; the look-at point closes 20% of a gap over 0.8 m, camera.md#update)
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Loading a level makes its textures, which needs an engine (one with no window).
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const Level99 loaded = loadLevel99(discPath);
    const std::optional<coney::human::PlayerStart> start = coney::human::researchedPlayerStart("level99");
    REQUIRE(start.has_value());
    Standing run(loaded, start.value_or(coney::human::PlayerStart{}));
    run.step(30);

    // Teleport 2 m to the side, then TeleportToFlag 2 m back: neither moves the camera on the spot.
    const coney::anim::Vec3 feet = run.player.human().position();
    for (const float side : {2.0F, 0.0F}) {
        const coney::anim::Vec3 eye = run.player.camera().position();
        const coney::anim::Vec3 lookAt = run.player.camera().lookAt();
        const coney::human::PlayerStart to{.position = coney::anim::Vec3{feet.x + side, feet.y, feet.z},
                                           .headingDegrees =
                                               start.value_or(coney::human::PlayerStart{}).headingDegrees};
        if (side > 0.0F) {
            run.player.place(run.mesh, to);
        } else {
            run.player.teleport(run.mesh, to);
        }
        CHECK(coney::anim::distance(run.player.camera().position(), eye) < 1e-4F);
        CHECK(coney::anim::distance(run.player.camera().lookAt(), lookAt) < 1e-4F);
        // The next update moves the look-at point a fifth of the 2 m, and the camera less than a step's worth.
        run.step(1);
        CHECK(std::abs(coney::anim::distance(run.player.camera().lookAt(), lookAt) - 0.4F) < 0.05F);
        CHECK(coney::anim::distance(run.player.camera().position(), eye) < 0.5F);
        run.step(30);
    }
}
