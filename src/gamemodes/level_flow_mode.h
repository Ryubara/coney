// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/profile_manager_mode.h"
#include "graphics/render_device.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;

/// Game mode 8, the level flow: the bottom of the stack. On its first entry it starts the front end (level 0,
/// `level100`: its scripts, the `menu` music and the level script's `Menu.onStart`, which shows the profile manager);
/// once the menus choose a level (`MenuLoadLevel`) it finishes the front end and starts that level. It never leaves.
///
/// Fields, as the original's (`+0x20`, `+0x24`, `+0x28`): the level chosen next (-1 none), whether the front-end level
/// is loaded, and "load the front end on the next resume" (set by enter(), cleared by the memory-card mode's exit
/// when this mode is below it).
///
/// Coney's stand-ins (docs/research/frontend.md#coneys-implementation):
/// - **`InitLevel`** does only its script step: the script system's level entry (`global.lua`, then `level100.lua`).
///   Nothing else of the level is loaded, so the background is black where the Wonder Wheel scene would be.
/// - **Without scripts** (no script system, or a `Menu.onStart` that does not show the menus) Coney shows the profile
///   manager itself with the two callbacks `Menu.onStart` passes, so the menus always come up.
/// - **Starting a chosen level**: gameplay (mode 1) does not exist yet. The update finishes the front end as the
///   original does (`Menu.onFinish`, then the unload, which makes a fresh Lua state), logs "level start requested" and
///   starts the front end again, so the player is back on the menus.
/// - The original's waits for the save system and its vsync and timer switches have nothing to act on.
///
/// Research: docs/research/frontend.md#mode-flow, docs/research/frontend.md#mode-8-fields
class LevelFlowMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 8;
    /// "No level chosen".
    static constexpr int kNoLevel = -1;
    /// The front end's level name, when the level table has no record 0.
    static constexpr std::string_view kFrontEndLevel = "level100";
    /// The Lua callbacks `Menu.onStart` hands to `ShowProfileManager`, for the stand-in without scripts.
    static constexpr std::string_view kOnRumble = "Menu.fadeToRMI";
    static constexpr std::string_view kOnStartGame = "Menu.startGame";

    /// Draws through `device`, pushes `profileManager` on `stack`, sends music to `services`, runs `scripts` and
    /// selects levels in `state`; each must outlive the mode. `log` gets a line when the front end starts or a level
    /// is asked for.
    LevelFlowMode(graphics::RenderDevice& device, GameModeStack& stack, ProfileManagerMode& profileManager,
                  FrontEndServices& services, script::ScriptSystem& scripts, GameState& state,
                  std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets "load the front end on resume" and resumes.
    /// @orig 0x0015c688 Mode8::Enter (unknown)
    void enter() override;

    /// Starts the front end when asked to and no level is chosen.
    /// @orig 0x0015c6f8 Mode8::Resume (unknown)
    void resume() override;

    /// One frame: with a level chosen, finishes the front end and (for now) starts it again; otherwise the front-end
    /// world's frame (black) and the scripts. Always stays.
    /// @orig 0x0015c858 Mode8::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// `MenuLoadLevel(name)`: chooses the level named `name` (by the level table) to start on the next update. A name
    /// the table does not have is logged and ignored.
    /// @orig 0x00160d78 MenuLoadLevel_Choose (unknown)
    /// @orig 0x0015c7b0 LevelFlow_ChooseLevel (unknown)
    void chooseLevel(std::string_view name);

    /// What the memory-card mode's exit does to this mode when it is below: no front end on the next resume.
    void cancelFrontEndLoad() { m_loadFrontEndOnResume = false; }

    /// The level chosen next (`+0x20`), kNoLevel for none.
    [[nodiscard]] int chosenLevel() const { return m_chosenLevel; }
    /// Whether the front-end level is loaded (`+0x24`).
    [[nodiscard]] bool frontEndLoaded() const { return m_frontEndLoaded; }
    /// Whether the next resume starts the front end (`+0x28`).
    [[nodiscard]] bool loadFrontEndOnResume() const { return m_loadFrontEndOnResume; }
    /// The level selected last (`level100` once the front end started; empty before).
    [[nodiscard]] const std::string& currentLevel() const { return m_currentLevel; }
    /// Levels the menus asked to start, oldest first (by name).
    [[nodiscard]] const std::vector<std::string>& levelRequests() const { return m_levelRequests; }

  private:
    /// Selects level 0, runs its scripts, starts the music `menu`, calls `Menu.onStart` and marks the front end loaded.
    /// @orig 0x0015c4b0 LevelFlow_StartFrontEnd (unknown)
    void startFrontEnd();

    /// Calls `Menu.onFinish` and unloads the front-end level: its part of `UnloadLevel` here is the fresh Lua state.
    /// @orig 0x0015c5f8 LevelFlow_FinishFrontEnd (unknown)
    void finishFrontEnd();

    graphics::RenderDevice& m_device;
    GameModeStack& m_stack;
    ProfileManagerMode& m_profileManager;
    FrontEndServices& m_services;
    script::ScriptSystem& m_scripts;
    GameState& m_state;
    std::function<void(std::string_view)> m_log;
    int m_chosenLevel = kNoLevel;
    bool m_frontEndLoaded = false;
    bool m_loadFrontEndOnResume = false;
    std::string m_currentLevel;
    std::vector<std::string> m_levelRequests;
};

} // namespace coney
