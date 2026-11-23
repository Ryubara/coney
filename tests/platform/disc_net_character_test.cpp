// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the host's play mode tells network play which character each human is
// (docs/guides/snapshots.md#character). In the sandbox's fight yard (`--play-level sandbox:combat`, level99's
// configuration) the player is Rembrandt and a fighter spawned as `warr_cl` is Cleon; the humans the mode hands to
// net::GameHumans each step carry their Character List indexes, so a client can draw each with his own model.
// Headless, on the fixed step with no clock. It runs only when the environment variable CONEY_DISC names the disc and
// skips otherwise; it prints counts only (LEGAL.md).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "animation/anim_math.h"
#include "characters/character_list.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "net/character_id.h"
#include "net/client_world.h"
#include "net/game_humans.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_play_scenery.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/test_paths.h"

// Expected: unverified, #1612 (Coney's own: snapshots.md#character; the models are the Character List's `warr_re_cv`
// (Rembrandt, type 32) and `warr_cl` (Cleon, type 1), docs/references/characters.md)
TEST_CASE("the host's play mode tells each human's character to network play", "[disc][net][character]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto list = coney::characters::loadCharacterList(*wad);
    REQUIRE(list.has_value());
    const auto rembrandt = list->indexOf("warr_re_cv");
    const auto cleon = list->indexOf("warr_cl");
    if (!rembrandt || !cleon) {
        FAIL("the character list names neither Rembrandt nor Cleon");
        return;
    }
    const std::int32_t rembrandtId = coney::net::characterIdOf(*rembrandt);
    const std::int32_t cleonId = coney::net::characterIdOf(*cleon);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());

    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun run =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(run.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(run.recorded);
    // Declared before the mode, which holds it: it must outlive the mode.
    coney::net::GameHumans humans;
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    auto made = coney::platform::createPlayInSandbox(
        **engine, *wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    coney::platform::PlayLevelMode& mode = **made;
    mode.shareHumansWith(&humans);

    coney::GameModeStack modes;
    coney::GameTimer timer;
    timer.setFixedStep(true);
    modes.push(mode);
    (void)modes.runUntilEmpty(timer, {}, 5);
    // The player alone, or with the yard's own fighters, all drawn as the scene's character: Rembrandt's.
    const auto charactersNow = [&humans] {
        std::set<std::int32_t> characters;
        for (const auto& [id, words] : humans.holding()) {
            characters.insert(coney::net::humanState(words).character);
        }
        return characters;
    };
    CHECK(charactersNow() == std::set<std::int32_t>{rembrandtId});

    // A fighter spawned as Cleon is Cleon in the snapshots, the others still Rembrandt.
    const coney::anim::Vec3 feet = mode.playerFeet();
    REQUIRE(
        mode.spawnFighterAs(coney::anim::Vec3{feet.x + 6.0F, feet.y, feet.z}, mode.playerHeadingDegrees(), "warr_cl"));
    (void)modes.runUntilEmpty(timer, {}, 5);
    CHECK(charactersNow() == std::set<std::int32_t>{rembrandtId, cleonId});

    // The player becomes Cleon (type 1): his own entry follows, as the mode draws him.
    REQUIRE(mode.changeCharacter(1));
    (void)modes.runUntilEmpty(timer, {}, 5);
    // The player's entry is the human standing where he does: changing character rebuilds the other fighters, so
    // his place among the ids is not fixed.
    const coney::anim::Vec3 now = mode.playerFeet();
    std::optional<coney::net::HumanState> player;
    for (const auto& [id, words] : humans.holding()) {
        const coney::net::HumanState state = coney::net::humanState(words);
        if (std::abs(state.position.x - now.x) < 0.5F && std::abs(state.position.y - now.y) < 0.5F) {
            player = state;
        }
    }
    if (!player) {
        FAIL("no human stands where the player does");
        return;
    }
    CHECK(player->character == cleonId);
    std::printf("host characters: %zu humans, %zu distinct characters\n", humans.holding().size(),
                charactersNow().size());
}
