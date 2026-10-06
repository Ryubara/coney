// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: the debug menus' Change character in the sandbox's fight yard
// (assets/sandbox/combat.layout), with level99's configuration as `--play-level sandbox:combat` takes it. The player is
// rebuilt as other character types where he stands, at full health, the fighters made again, and play goes on; then a
// sample of every configured type is tried. Headless, on the fixed step with no clock. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "characters/character_types.h"
#include "combat/anim_ranges.h"
#include "combat/meters.h"
#include "core/game_timer.h"
#include "debug/play_controls.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"

namespace {

// The damage the player's first square (`ANIM_ATTACK_S1`, anim id 12) deals.
int punchDamage(const coney::platform::PlayLevelMode& mode) {
    const coney::combat::AnimRangeList* ranges = mode.player().human().ranges();
    return ranges == nullptr ? -1 : ranges->damage(12);
}

// Whether the follow camera is behind the player: its eye on the far side of his feet from where he faces.
bool cameraBehind(const coney::platform::PlayLevelMode& mode) {
    const coney::human::PlayerSnapshot& now = mode.player().current();
    const float heading = mode.player().human().heading();
    const float dx = now.cameraEye.x - mode.playerFeet().x;
    const float dy = now.cameraEye.y - mode.playerFeet().y;
    return (dx * -std::sin(heading)) + (dy * std::cos(heading)) < 0.0F;
}

} // namespace

TEST_CASE("Change character rebuilds the player as another type where he stands", "[disc][debug]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());

    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun run =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(run.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(run.recorded);
    auto world = coney::sandbox::SandboxWorld::load(std::filesystem::path(CONEY_ASSETS_DIR) / "sandbox", "combat");
    REQUIRE(world.has_value());
    auto made = coney::platform::PlayLevelMode::createInSandbox(
        **engine, *wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    coney::platform::PlayLevelMode& mode = **made;
    const std::vector<coney::debug::CharacterChoice> choices = mode.characterChoices();
    std::printf("change character: %zu configured types, %zu a player can be\n", setup.types.all().size(),
                choices.size());
    REQUIRE(choices.size() > 100);
    CHECK(mode.playerType() == 32);
    // The level's own player takes his class as a changed one does: one path (human::playerClassOf()).
    const int startS1 = punchDamage(mode);
    const int startPower = mode.player().human().fighterProfile().powerClass.powerMax;

    // A few steps of play, a fighter in front of him, then Ash (type 40) where he stands.
    coney::GameModeStack modes;
    coney::GameTimer timer;
    timer.setFixedStep(true);
    modes.push(mode);
    (void)modes.runUntilEmpty(timer, {}, 15);
    REQUIRE(mode.spawnFighter(coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F),
                              mode.playerHeadingDegrees() + 180.0F));
    const std::size_t fighters = mode.fighterCount();
    const coney::anim::Vec3 feet = mode.playerFeet();
    const float heading = mode.playerHeadingDegrees();
    REQUIRE(mode.changeCharacter(40));
    CHECK(mode.playerType() == 40);
    CHECK(mode.model() == "warr_ty_cv");
    CHECK(std::abs(mode.playerFeet().x - feet.x) < 0.01F);
    CHECK(std::abs(mode.playerFeet().y - feet.y) < 0.01F);
    CHECK(std::abs(mode.playerFeet().z - feet.z) < 0.05F);
    CHECK(std::abs(mode.playerHeadingDegrees() - heading) < 0.01F);
    const coney::combat::Health& health = mode.player().human().health();
    CHECK(health.value() == health.maximum());
    CHECK(mode.fighterCount() == fighters);
    CHECK(cameraBehind(mode));
    // He plays on, and a plain alias (31) is drawn as its class's model, as the original draws a player.
    (void)modes.runUntilEmpty(timer, {}, 15);
    REQUIRE(mode.changeCharacter(31));
    CHECK(mode.model() == "warr_re");
    CHECK(cameraBehind(mode));
    // Back to his start type: the same damage and power class as the level gave him.
    REQUIRE(mode.changeCharacter(32));
    CHECK(punchDamage(mode) == startS1);
    CHECK(mode.player().human().fighterProfile().powerClass.powerMax == startPower);
    std::printf("change character: type 32's square plays %d damage, power maximum %d\n", startS1, startPower);
    REQUIRE(mode.changeCharacter(31));
    // A type the configuration lacks leaves him as he was.
    CHECK_FALSE(mode.changeCharacter(100000));
    CHECK(mode.playerType() == 31);

    // Every 16th type a player can be (all of them take over a minute), each loaded and played a step.
    std::size_t tried = 0;
    std::size_t changed = 0;
    for (std::size_t i = 0; i < choices.size(); i += 16) {
        ++tried;
        if (mode.changeCharacter(choices[i].type)) {
            ++changed;
            (void)modes.runUntilEmpty(timer, {}, 1);
        }
    }
    std::printf("change character: %zu of %zu types tried loaded and played\n", changed, tried);
    CHECK(changed == tried);
}
