// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/ai_config.h"

#include <cstdio>
#include <optional>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/level_start.h"
#include "scripting/config_strings.h"

// Against the player's own disc: the AI fighters' configuration read from the calls `level99`'s scripts make, run alone
// as the sandbox runs them, matches what the analyst read at runtime for the sparring Warriors (class 58, power class
// 40; docs/research/ai.md#level99). Runs only when CONEY_DISC names the disc; prints counts only (LEGAL.md).

namespace {

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

} // namespace

TEST_CASE("level99's scripts configure the sparring Warriors as the runtime read them", "[ai][disc]") {
    const std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set");
    }
    const coney::LevelScriptRun run =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level99", 1, {});
    const coney::ai::AiConfig config = coney::ai::aiConfigFrom(run.recorded);
    std::printf("  level99: %zu AI configuration calls read\n", config.callsRead);
    CHECK(config.callsRead > 0);
    CHECK(config.fighter.brain == coney::ai::BrainType::Gang);
    CHECK(config.fighter.health == 1400);
    // Power class 40: the attack delay's factors 20 (a 4 s wait after a 200 ms kind), block 0.2, counter 0.08.
    CHECK(config.powerClass.attackDelayFactor == 20.0F);
    CHECK(config.powerClass.attackDelayDownFactor == 20.0F);
    CHECK(config.powerClass.blockChance == 0.2F);
    CHECK(config.powerClass.hurtBlockChance == 0.1F);
    CHECK(config.powerClass.counterChance == 0.08F);
    CHECK(config.settings.baseBlockChance == 60);
    CHECK(config.settings.attackDelaysMs == coney::ai::referenceAttackDelays());
}
