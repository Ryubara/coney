// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;
class LevelFlowMode;

/// Game mode 0xb, the mission-complete mode. `HUDLaunchMissionComplete(kind)` pushes it over the mode on top (mode 1 at
/// the end of a mission; mode 8 once, when a story game starts: `runNextMission(1)` in `Menu.startGame`). Its enter
/// calls the Lua function `UnlockAndLoad` (the unlocks, then `runNextMission(1)`, which chooses the next level); its
/// update runs one frame of the world and, once a kind is set, leaves and acts on the kind: 1 puts the checkpoint back
/// to 1, 2 chooses the current level again, 3 the next record; and when the mode below is gameplay (mode 1), that goes
/// too, so the level flow starts the chosen level. Kind 4, the story start's, is none of these.
///
/// Coney's stand-ins (docs/research/frontend.md#coneys-implementation):
/// - There is no mission-complete screen and no save system: the original's inventories, its save-system call and the
///   autosave it asks for after the pop have nothing to act on.
/// - There are no humans to put into a still state, and no world: the frame is black.
/// - The kind is stored on every launch, whether or not the mode was already on top.
///
/// Research: docs/research/frontend.md#story-start, docs/research/boot.md#one-frame
class MissionCompleteMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0xb;
    /// The Lua function enter() calls.
    static constexpr std::string_view kUnlockAndLoad = "UnlockAndLoad";
    /// The kinds the update acts on.
    static constexpr int kKindCheckpointOne = 1;
    static constexpr int kKindReloadLevel = 2;
    static constexpr int kKindNextLevel = 3;

    /// Draws through `device`, pops itself (and gameplay below it) off `stack`, chooses levels in `levelFlow`, calls
    /// `scripts` and resets the checkpoint in `state`; each must outlive the mode. `log` gets a line per launch.
    MissionCompleteMode(graphics::RenderDevice& device, GameModeStack& stack, LevelFlowMode& levelFlow,
                        script::ScriptSystem& scripts, GameState& state, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `HUDLaunchMissionComplete(kind)`: stores `kind` and pushes the mode unless it is already on top.
    /// @orig 0x0015d420 MissionComplete_Launch (unknown)
    void launch(int kind);

    /// Calls `UnlockAndLoad`.
    /// @orig 0x0015cf70 ModeB::Enter (unknown)
    void enter() override;

    /// One frame of the world (the scripts' step); once the kind is set, pops this mode and acts on it. Always stays:
    /// the pop is its own, as the original's.
    /// @orig 0x0015d160 ModeB::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// The world's frame: no world here, so black, presented.
    void render(const RenderTime& time) override;

    /// Forgets the kind, so the next launch starts clear.
    void exit() override { m_kind = 0; }

    /// The kind of the last launch (`+0x24`), 0 for none.
    [[nodiscard]] int kind() const { return m_kind; }
    /// Launches since start-up.
    [[nodiscard]] std::uint64_t launches() const { return m_launches; }

  private:
    graphics::RenderDevice& m_device;
    GameModeStack& m_stack;
    LevelFlowMode& m_levelFlow;
    script::ScriptSystem& m_scripts;
    GameState& m_state;
    std::function<void(std::string_view)> m_log;
    int m_kind = 0;
    std::uint64_t m_launches = 0;
};

} // namespace coney
