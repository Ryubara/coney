// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the disc tests that play through the front end share: the disc named by CONEY_DISC, gameplay's level loader as
// `main` sets it up, and QUICK RUMBLE from boot as a scripted pad drives it (docs/guides/building.md#run-coney). They
// print counts and positions only (LEGAL.md).

#include <array>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "characters/character_types.h"
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
#include "scripting/script_bindings.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world/sector_budget.h"

namespace coney::test {

/// The disc named by CONEY_DISC, opened; nothing when it is not set.
inline std::optional<io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<io::Wad>(std::move(*wad)) : std::nullopt;
}

/// Gameplay's level loader as main sets it up: the play mode with player 1 where the scripts left him, as the
/// character his type names, not snapped after a teleport, with the fighters and character types the scripts
/// configured; the play mode's lines go to `print` (none when empty).
inline GameplayMode::LevelLoader playLoader(platform::RenderEngine& renderer, const io::Wad& wad,
                                            world::SectorBudget& budget,
                                            std::function<void(std::string_view)> print = {}) {
    return [&renderer, &wad, &budget, print = std::move(print)](
               const LevelStart& start, const ScriptedCast& cast) -> std::expected<std::unique_ptr<GameMode>, Error> {
        std::optional<human::PlayerStart> playerStart;
        platform::PlayerSetup setup;
        if (start.player) {
            const HumanCreation& player = *start.player;
            const std::array<float, 3> p =
                player.teleported ? player.teleported->position : player.position.value_or(std::array<float, 3>{});
            playerStart = human::PlayerStart{.position = anim::Vec3{p[0], p[1], p[2]},
                                             .headingDegrees = player.teleported ? player.teleported->headingDegrees
                                                                                 : player.headingDegrees};
            setup.model = player.model.empty() ? std::string(human::kPlayerModel) : player.model;
            setup.snapToGround = !player.teleported;
        }
        if (cast.recorded != nullptr) {
            setup.ai = ai::aiConfigFrom(*cast.recorded);
            setup.types = characters::CharacterTypes::fromRecorded(*cast.recorded);
        }
        auto mode = platform::PlayLevelMode::create(
            renderer, wad, start.level, budget,
            [print](std::string_view line) {
                if (print) {
                    print(line);
                }
            },
            playerStart, setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        return std::unique_ptr<GameMode>(std::move(*mode));
    };
}

/// The pad of QUICK RUMBLE from boot with the default set-up (1 ON 1, one player, the Furies against the Orphans, the
/// Fight Pen): START, the stick up most of the way (wrapping to QUICK RUMBLE), cross; cross on each of the Rumble
/// menu's screens, twice on Choose Gangs; then cross on the intro's prompt a few times, to be sure of meeting it. The
/// countdown ends by frame 600; a test adds its own lines after it.
inline constexpr std::string_view kQuickRumbleScript = "200 tap start\n"
                                                       "212 stick left 0 70\n"
                                                       "214 stick left 0 0\n"
                                                       "225 tap cross\n"
                                                       "280 tap cross\n"
                                                       "290 tap cross\n"
                                                       "300 tap cross\n"
                                                       "305 tap cross\n"
                                                       "310 tap cross\n"
                                                       "630 tap cross\n"
                                                       "670 tap cross\n"
                                                       "710 tap cross\n"
                                                       "750 tap cross\n"
                                                       "790 tap cross\n";

/// The game from boot over the disc, headless (the null renderer), in test mode (fixed step), with the pad `script`
/// (kQuickRumbleScript and a test's own lines): the start-up flow, its mode stack and its log.
class DiscGame {
  public:
    /// Everything set up over `wad` (which must outlive it) and started; the pad script must parse.
    DiscGame(const io::Wad& wad, std::string_view script)
        : m_wad(wad), m_handlers(chunk::ChunkHandlerTable::withDefaults()), m_budget(world::kSectorPoolSize) {
        platform::addTextureDictionaryHandlers(m_handlers);
        platform::addSpriteSheetHandlers(m_handlers);
        auto engine = platform::RenderEngine::start(platform::RenderBackend::Null, {});
        REQUIRE(engine.has_value());
        m_engine = std::move(*engine);
        auto parsed = parseInputScript(script);
        REQUIRE(parsed.has_value());
        m_input = std::make_unique<ScriptedInput>(std::move(*parsed));
        m_stack.setInput(m_input.get());
        m_flow = std::make_unique<StartUpFlow>(
            *m_engine, m_stack,
            [this](std::string_view name) -> std::expected<graphics::SpriteSheet, Error> {
                return platform::loadSpriteSheetResource(m_wad, m_handlers, name, false);
            },
            m_strings, LegalScreenSettings{}, [this](std::string_view line) { m_log.emplace_back(line); },
            script::wadScriptSource(m_wad),
            playLoader(*m_engine, m_wad, m_budget, [this](std::string_view line) { m_log.emplace_back(line); }));
        m_flow->start();
        m_timer.setFixedStep(true);
    }
    DiscGame(const DiscGame&) = delete;
    DiscGame& operator=(const DiscGame&) = delete;
    DiscGame(DiscGame&&) = delete;
    DiscGame& operator=(DiscGame&&) = delete;
    ~DiscGame() = default;

    /// Runs `frames` more frames.
    void run(std::uint64_t frames) { m_frames += m_stack.runUntilEmpty(m_timer, {}, frames); }
    /// Runs until the mode `id` is on top, at most `limit` more frames; whether it got there.
    bool runUntilTop(std::uint32_t id, std::uint64_t limit) {
        constexpr std::uint64_t kChunk = 15;
        for (std::uint64_t done = 0; done < limit && m_stack.topId() != id; done += kChunk) {
            run(kChunk);
        }
        return m_stack.topId() == id;
    }

    /// Runs kQuickRumbleScript's menus (to frame 330, while the Rumble menu fades out) and then makes the fight game
    /// type `gameType` (a mode's number) in arena `level` with `gangSize` fighters a side, as the Rumble menu would
    /// have set it had the profile unlocked them (frontend.md#rumble-setup).
    void chooseRumble(std::uint16_t gameType, int level, std::uint16_t gangSize) {
        constexpr std::uint64_t kMenuFadingOut = 330;
        run(kMenuFadingOut - m_frames);
        RumbleSetup& rumble = m_flow->state().rumble;
        rumble.values.at(RumbleSetup::kGameType) = gameType;
        rumble.values.at(RumbleSetup::kGangSize) = gangSize;
        rumble.levelNumber = level;
    }

    [[nodiscard]] GameModeStack& stack() { return m_stack; }
    [[nodiscard]] StartUpFlow& flow() { return *m_flow; }
    [[nodiscard]] const std::vector<std::string>& log() const { return m_log; }
    /// Frames run so far.
    [[nodiscard]] std::uint64_t frames() const { return m_frames; }
    /// The play mode of the level on top of gameplay; null when none is loaded.
    [[nodiscard]] const platform::PlayLevelMode* play() {
        return dynamic_cast<const platform::PlayLevelMode*>(m_flow->gameplay().level());
    }

  private:
    const io::Wad& m_wad;
    chunk::ChunkHandlerTable m_handlers;
    std::unique_ptr<platform::RenderEngine> m_engine;
    gui::GlobalStrings m_strings;
    world::SectorBudget m_budget;
    std::unique_ptr<ScriptedInput> m_input;
    GameModeStack m_stack;
    std::vector<std::string> m_log;
    std::unique_ptr<StartUpFlow> m_flow; // after everything it refers to
    GameTimer m_timer;
    std::uint64_t m_frames = 0;
};

} // namespace coney::test
