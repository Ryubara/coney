// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: (1) the level scripts, run alone as `--play-level` runs them, put player 1
// where the research says for a few known checkpoints (values written below, from docs/references/level-starts.md);
// (2) STORY from the main menu reaches Rembrandt standing at level99's checkpoint 1 under the pad's control, headless,
// through the original's modes (0x12, 0xb, 8, 1) with the game's own scripts. They run only when the environment
// variable CONEY_DISC names the disc and skip otherwise; they print counts and positions only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "scripting/config_strings.h"
#include "world/sector_budget.h"

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

// One start the research lists: the level, the checkpoint, player 1's name, type, position and heading.
struct KnownStart {
    std::string_view level;
    int checkpoint;
    std::string_view name;
    int type;
    std::array<float, 3> position;
    float heading;
};

// A few entries of docs/references/level-starts.md, across levels, characters and an interior below the street.
constexpr std::array kKnownStarts{
    KnownStart{"level99", 1, "Rembrandt", 32, {-284.4F, 120.4F, 0.3F}, 0.0F},
    KnownStart{"level99", 2, "Rembrandt", 30, {46.0F, 34.2F, 0.3F}, 154.0F},
    KnownStart{"level2", 3, "Cleon", 1, {445.3F, -45.1F, 8.0F}, 297.0F},
    KnownStart{"level3", 4, "Snow", 33, {-268.5F, 353.6F, 16.6F}, 1.0F},
    KnownStart{"level5", 2, "Ajax", 11, {14.4F, 2.4F, -200.0F}, 90.0F},
};

} // namespace

TEST_CASE("the disc's level scripts put player 1 at the researched start of each checkpoint", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    for (const KnownStart& known : kKnownStarts) {
        INFO(std::string(known.level) << " checkpoint " << known.checkpoint);
        const coney::LevelScriptRun run =
            coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), known.level, known.checkpoint, {});
        REQUIRE(run.start.player.has_value());
        const coney::HumanCreation player = run.start.player.value_or(coney::HumanCreation{});
        CHECK(player.name == known.name);
        CHECK(player.type == known.type);
        CHECK(player.headingDegrees == known.heading);
        REQUIRE(player.position.has_value());
        const std::array<float, 3> position = player.position.value_or(std::array<float, 3>{});
        for (std::size_t axis = 0; axis < position.size(); ++axis) {
            CHECK(std::abs(position.at(axis) - known.position.at(axis)) < 0.01F);
        }
        std::printf("  %.*s checkpoint %d: %zu humans, %llu script errors, %llu skipped calls\n",
                    static_cast<int>(known.level.size()), known.level.data(), known.checkpoint, run.humans,
                    static_cast<unsigned long long>(run.scriptErrors),
                    static_cast<unsigned long long>(run.skippedCalls));
    }
}

TEST_CASE("the disc's STORY reaches Rembrandt standing in level99 under the pad's control", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // What main sets up: the chunk handlers, the headless renderer, the sheet loader, the sector budget.
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(handlers);
    coney::platform::addSpriteSheetHandlers(handlers);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::platform::RenderEngine& renderer = **engine;
    const coney::io::Wad& theWad = *wad;
    coney::gui::GlobalStrings strings;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);

    // START, cross on STORY (selected first), cross on the PM_Profile stand-in; then, in level99, the left stick at
    // 35 % forward for two seconds and at a 30/65 diagonal for two more, then let go.
    auto script = coney::parseInputScript("200 tap start\n"
                                          "235 tap cross\n"
                                          "265 tap cross\n"
                                          "330 stick left 0 35\n"
                                          "390 stick left 30 65\n"
                                          "450 stick left 0 0\n");
    REQUIRE(script.has_value());
    coney::ScriptedInput input(std::move(*script));
    coney::GameModeStack stack;
    stack.setInput(&input);
    std::vector<std::string> log;
    coney::StartUpFlow flow(
        renderer, stack,
        [&theWad, &handlers](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
            return coney::platform::loadSpriteSheetResource(theWad, handlers, name, false);
        },
        strings, coney::LegalScreenSettings{}, [&log](std::string_view line) { log.emplace_back(line); },
        coney::script::wadScriptSource(*wad),
        [&renderer, &theWad,
         &budget](const coney::LevelStart& start) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
            std::optional<coney::human::PlayerStart> playerStart;
            if (start.player && start.player->position) {
                const std::array<float, 3> p = start.player->position.value_or(std::array<float, 3>{});
                playerStart = coney::human::PlayerStart{.position = coney::anim::Vec3{p[0], p[1], p[2]},
                                                        .headingDegrees = start.player->headingDegrees};
            }
            auto mode = coney::platform::PlayLevelMode::create(
                renderer, theWad, start.level, budget, [](std::string_view) {}, playerStart);
            if (!mode) {
                return std::unexpected(std::move(mode.error()));
            }
            return std::unique_ptr<coney::GameMode>(std::move(*mode));
        });
    flow.start();
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // Through the menus into the level: by frame 300 gameplay is on top with level99 loaded.
    stack.runUntilEmpty(timer, {}, 300);
    for (const std::string& line : log) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(stack.topId() == coney::GameplayMode::kId);
    CHECK(flow.missionComplete().launches() == 2);
    CHECK(flow.state().checkPoint == 1.0);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(flow.gameplay().level());
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return;
    }
    CHECK(play->sceneName() == "level99");
    // Rembrandt stands where level99.lua's HuCreate put him, on the ground below it (z 0.25 at runtime).
    const coney::anim::Vec3 feet = play->player().human().position();
    CHECK(std::abs(feet.x - -284.4F) < 0.01F);
    CHECK(std::abs(feet.y - 120.4F) < 0.01F);
    CHECK(std::abs(feet.z - 0.25F) < 0.05F);
    CHECK(!play->player().human().airborne());

    // The pad moves him, and he stands again when the stick is let go.
    stack.runUntilEmpty(timer, {}, 200);
    CHECK(play->stats().travelled > 3.0F);
    CHECK(play->player().human().speed() == 0.0F);
    CHECK(!play->player().human().airborne());
    CHECK(flow.scripts().errors() == 0);
    std::printf("  story: %zu log lines, %zu humans created, %llu Lua states, %llu skipped calls; %s", log.size(),
                flow.humans().all().size(), static_cast<unsigned long long>(flow.scripts().generation()),
                static_cast<unsigned long long>(flow.scripts().skippedCalls()), play->summary().c_str());
}
