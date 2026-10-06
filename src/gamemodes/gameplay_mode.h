// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ai/brains.h"
#include "ai/scripted_brains.h"
#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/level_start.h"
#include "graphics/render_device.h"
#include "scripting/message_handlers.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/volume_boxes.h"

namespace coney {

/// A loaded level whose player 1 a script can move while it plays: a `TeleportToFlag` on player 1 after the level
/// loaded (the hub's door walk starts 100 ms into play) reaches the level through this. The play mode implements it.
class ScriptedPlayer {
  public:
    virtual ~ScriptedPlayer() = default;
    ScriptedPlayer() = default;
    ScriptedPlayer(const ScriptedPlayer&) = delete;
    ScriptedPlayer& operator=(const ScriptedPlayer&) = delete;
    ScriptedPlayer(ScriptedPlayer&&) = delete;
    ScriptedPlayer& operator=(ScriptedPlayer&&) = delete;

    /// Puts player 1 at `placement` (feet, heading in degrees) with no ground snap, as `TeleportToFlag` does.
    virtual void teleportPlayer(const world_objects::Placement& placement) = 0;
};

/// What a level's scripts drive, which gameplay gives the level it loads: the humans the scripts create and the brains
/// and gangs they give goals to. The scripts' hold on the brains holds their calls until the level makes the humans
/// (ai::ScriptedBrains::release()). Everything is gameplay's, valid until the level is unloaded.
///
/// Research: docs/research/ai.md#coney, docs/research/characters.md#creation
struct ScriptedCast {
    CreatedHumans* humans = nullptr;                 ///< The scripts' humans, in the order of their `HuCreate` calls.
    const script::RecordedCalls* recorded = nullptr; ///< The configuration the scripts recorded (the classes).
    ai::Brains* brains = nullptr;                    ///< The level's brains, gangs and formations.
    ai::ScriptedBrains* scripted = nullptr;          ///< The scripts' hold on them.
};

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
/// - The rest of `InitLevel` (the object list, the dependency list, the music, the intro movie `L99_IN`) and of mode
///   1's enter (audio, the level-end countdown) is not there yet: the player has control on the first frame. The
///   start callback runs before the level loads (runLevelScript()).
/// - The level's brains and gangs are made before its script (the AI host of `context`), but the humans the script
///   creates are made, and the calls on them run, only once the level has loaded its characters (ScriptedCast).
/// - Each frame after the level's step: the volume boxes' trigger update over the scripts' humans (their messages to
///   the objects' handlers in `context`), then the scripts' frame (scheduled calls and the stopwatch).
/// - A teleport of player 1 by a script during play (HumanCreation::teleports changes) is handed to the level when it
///   is a ScriptedPlayer.
/// - A level that fails to load leaves the frame black, with the error logged.
/// - Leaving (exit) is `UnloadLevel`'s script part only: a fresh Lua state.
///
/// Research: docs/research/level-loading.md#mode-1, docs/research/level-loading.md#story-into-level99
class GameplayMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 1;

    /// Loads the level `start` names, with player 1 at `start` (or the platform's stand-in start when the script made
    /// none), as a mode this one runs, the scripts' humans made from `cast`. Fails as the level's loaders do.
    using LevelLoader = std::function<std::expected<std::unique_ptr<GameMode>, Error>(const LevelStart& start,
                                                                                      const ScriptedCast& cast)>;

    /// Draws through `device` while no level is loaded, runs the level scripts in `scripts` (whose bindings work on
    /// `context`, where each level's brains are set as the AI host), reads the checkpoint and the start callback in
    /// `state`, keeps the scripts' humans in `humans` and flags in `flags`, and loads levels with `loader`; each must
    /// outlive the mode. `recorded` is the configuration the scripts record. `log` gets a line for the start and for a
    /// level that fails.
    GameplayMode(graphics::RenderDevice& device, script::ScriptSystem& scripts, script::BindingContext& context,
                 GameState& state, CreatedHumans& humans, world_objects::WorldFlags& flags,
                 const script::RecordedCalls& recorded, LevelLoader loader, std::function<void(std::string_view)> log);
    ~GameplayMode() override;
    GameplayMode(const GameplayMode&) = delete;
    GameplayMode& operator=(const GameplayMode&) = delete;
    GameplayMode(GameplayMode&&) = delete;
    GameplayMode& operator=(GameplayMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets the level the next enter loads (the level flow's selection).
    void setLevel(std::string level) { m_levelName = std::move(level); }

    /// `InitLevel`: the level's brains, the level script, then the level from the loader, entered (its preload).
    /// @orig 0x001582e0 Mode1::Enter (unknown)
    /// @orig 0x0015fe90 InitLevel (InitLevel.cpp)
    void enter() override;

    /// A frame of play: the loaded level's step, then the scripts' frame, then a teleport of player 1 the scripts made
    /// handed to the level. Leaves when the level does.
    /// @orig 0x00158728 Mode1::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// The level's frame, or black while none is loaded.
    void render(const RenderTime& time) override;

    /// Ends the level, its brains and gangs, and makes a fresh Lua state (`UnloadLevel`'s script part).
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
    /// The level's brains and gangs; null while no level is entered.
    [[nodiscard]] const ai::Brains* brains() const { return m_brains.get(); }

  private:
    // Drops the level, then the scripts' hold on its brains (no longer the AI host), then the brains.
    void endLevel();
    // The volume boxes' trigger update over the scripts' humans, their messages going to the objects' handlers
    // (docs/research/scripting.md#triggers). In the original the boxes update with the other tasks in the world step.
    void updateBoxes(std::uint64_t nowMs);

    graphics::RenderDevice& m_device;
    script::ScriptSystem& m_scripts;
    script::BindingContext& m_context;
    GameState& m_state;
    CreatedHumans& m_humans;
    world_objects::WorldFlags& m_flags;
    const script::RecordedCalls& m_recorded;
    LevelLoader m_loader;
    std::function<void(std::string_view)> m_log;
    std::string m_levelName;
    std::optional<LevelStart> m_start;
    // The level's brains and the scripts' hold on them, before the level, whose humans they refer to.
    std::unique_ptr<ai::Brains> m_brains;
    std::unique_ptr<ai::ScriptedBrains> m_scripted;
    std::unique_ptr<GameMode> m_level;
    std::uint32_t m_playerTeleports = 0; // player 1's teleports the level has been told of
};

} // namespace coney
