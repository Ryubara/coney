// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/profile_manager_mode.h"
#include "graphics/render_device.h"

namespace coney {

class GameModeStack;

/// Game mode 8, the level flow: the bottom of the stack. On its first entry it starts the front end (the front-end
/// level, the `menu` music and the level script's `Menu.onStart`, which shows the profile manager); later it starts
/// the level the menus chose. It never leaves.
///
/// Fields, as the original's (`+0x20`, `+0x24`, `+0x28`): the level chosen next (-1 none), whether the front-end level
/// is loaded, and "load the front end on the next resume" (set by enter(), cleared by the memory-card mode's exit
/// when this mode is below it).
///
/// Coney's stand-ins (docs/research/frontend.md#coneys-implementation):
/// - **The front-end level** (`level100`, level index 0) is selected by name, not loaded: there is no level table
///   (`CfgLevelName` in `config_preload3.lua`) and no `InitLevel` yet. Its update clears the screen to black where the
///   original draws the front-end world.
/// - **`Menu.onStart`** is not run: the level scripts need a script system Coney does not have. Coney does the one
///   thing the profile manager needs from it, `ShowProfileManager("Menu.fadeToRMI", "Menu.startGame")`.
/// - Starting a chosen level (mode 1) does not exist yet; nothing chooses one.
/// - The original's waits for the save system and its vsync and timer switches have nothing to act on.
///
/// Research: docs/research/frontend.md#mode-flow, docs/research/frontend.md#mode-8-fields
class LevelFlowMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 8;
    /// "No level chosen".
    static constexpr int kNoLevel = -1;
    /// Level index 0, the front end.
    static constexpr std::string_view kFrontEndLevel = "level100";
    /// The Lua callbacks `Menu.onStart` hands to `ShowProfileManager`.
    static constexpr std::string_view kOnRumble = "Menu.fadeToRMI";
    static constexpr std::string_view kOnStartGame = "Menu.startGame";

    /// Draws through `device`, pushes `profileManager` on `stack` and sends music to `services`; each must outlive
    /// the mode. `log` gets a line when the front end starts.
    LevelFlowMode(graphics::RenderDevice& device, GameModeStack& stack, ProfileManagerMode& profileManager,
                  FrontEndServices& services, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets "load the front end on resume" and resumes.
    /// @orig 0x0015c688 Mode8::Enter (unknown)
    void enter() override;

    /// Starts the front end when asked to and no level is chosen.
    /// @orig 0x0015c6f8 Mode8::Resume (unknown)
    void resume() override;

    /// One frame of the front-end world (black for now). Always stays. A stand-in for the original's `Update`
    /// (`0x0015c858`), which draws the world or starts the chosen level, so it carries no original-function tag.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// What the memory-card mode's exit does to this mode when it is below: no front end on the next resume.
    void cancelFrontEndLoad() { m_loadFrontEndOnResume = false; }

    /// The level chosen next (`+0x20`), kNoLevel for none.
    [[nodiscard]] int chosenLevel() const { return m_chosenLevel; }
    /// Whether the front-end level is "loaded" (`+0x24`).
    [[nodiscard]] bool frontEndLoaded() const { return m_frontEndLoaded; }
    /// Whether the next resume starts the front end (`+0x28`).
    [[nodiscard]] bool loadFrontEndOnResume() const { return m_loadFrontEndOnResume; }
    /// The level selected last (`level100` once the front end started; empty before).
    [[nodiscard]] std::string_view currentLevel() const { return m_currentLevel; }

  private:
    /// Selects level 0, starts the music `menu`, runs `Menu.onStart`'s stand-in and marks the front end loaded.
    /// @orig 0x0015c4b0 LevelFlow_StartFrontEnd (unknown)
    void startFrontEnd();

    graphics::RenderDevice& m_device;
    GameModeStack& m_stack;
    ProfileManagerMode& m_profileManager;
    FrontEndServices& m_services;
    std::function<void(std::string_view)> m_log;
    int m_chosenLevel = kNoLevel;
    bool m_frontEndLoaded = false;
    bool m_loadFrontEndOnResume = false;
    std::string_view m_currentLevel;
};

} // namespace coney
