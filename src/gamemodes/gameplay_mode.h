// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/level_start.h"
#include "graphics/render_device.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"

namespace coney {

/// Game mode 1, gameplay: one loaded level. The level flow (mode 8) selects a level and pushes it; its enter runs
/// `InitLevel`, and each update is a frame of play.
///
/// `InitLevel` here is its script step and the level's loading, in the original's order where it matters: the level
/// script runs first (runLevelScript(): `global.lua`, then `<level>.lua`, whose `HuCreate` makes player 1 at the
/// checkpoint's start), then the level loads with the player at that start and preloads around him. The loading,
/// drawing and the player are the platform's: a LevelLoader makes them as a mode of their own, which this mode runs
/// in its place (Coney's play mode, `src/platform/play_level_mode.h`).
///
/// Coney's stand-ins (docs/research/level-loading.md#coneys-implementation):
/// - The rest of `InitLevel` (the object list, the dependency list, the music, the intro movie `L99_IN`, the start
///   callback `StartAmbient` and its intro scene) and of mode 1's enter (audio, the level-end countdown) is not there
///   yet: the player has control on the first frame.
/// - A level that fails to load leaves the frame black, with the error logged.
/// - Leaving (exit) is `UnloadLevel`'s script part only: a fresh Lua state.
///
/// Research: docs/research/level-loading.md#mode-1, docs/research/level-loading.md#story-into-level99
class GameplayMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 1;

    /// Loads the level `start` names, with player 1 at `start` (or the platform's stand-in start when the script made
    /// none), as a mode this one runs. Fails as the level's loaders do.
    using LevelLoader = std::function<std::expected<std::unique_ptr<GameMode>, Error>(const LevelStart& start)>;

    /// Draws through `device` while no level is loaded, runs the level scripts in `scripts`, reads the checkpoint in
    /// `state`, keeps the scripts' humans in `humans` and loads levels with `loader`; each must outlive the mode. `log`
    /// gets a line for the start and for a level that fails.
    GameplayMode(graphics::RenderDevice& device, script::ScriptSystem& scripts, const GameState& state,
                 CreatedHumans& humans, LevelLoader loader, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets the level the next enter loads (the level flow's selection).
    void setLevel(std::string level) { m_levelName = std::move(level); }

    /// `InitLevel`: the level script, then the level from the loader, entered (its preload).
    /// @orig 0x001582e0 Mode1::Enter (unknown)
    /// @orig 0x0015fe90 InitLevel (InitLevel.cpp)
    void enter() override;

    /// A frame of play: the loaded level's step, then the scripts' frame. Leaves when the level does.
    /// @orig 0x00158728 Mode1::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// The level's frame, or black while none is loaded.
    void render(const RenderTime& time) override;

    /// Ends the level and makes a fresh Lua state (`UnloadLevel`'s script part).
    void exit() override;

    /// Passed on to the loaded level.
    void suspend() override;
    void resume() override;

    /// Whether a level loader was given: without one, every level fails to load.
    [[nodiscard]] bool loads() const { return static_cast<bool>(m_loader); }
    /// The loaded level's mode; null before enter or when the level failed to load.
    [[nodiscard]] GameMode* level() const { return m_level.get(); }
    /// Where the level script started player 1 (set by enter).
    [[nodiscard]] const std::optional<LevelStart>& start() const { return m_start; }

  private:
    graphics::RenderDevice& m_device;
    script::ScriptSystem& m_scripts;
    const GameState& m_state;
    CreatedHumans& m_humans;
    LevelLoader m_loader;
    std::function<void(std::string_view)> m_log;
    std::string m_levelName;
    std::optional<LevelStart> m_start;
    std::unique_ptr<GameMode> m_level;
};

} // namespace coney
