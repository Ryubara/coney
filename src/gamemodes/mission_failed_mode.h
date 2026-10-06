// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "core/interpolation.h"
#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/pause_mode.h"
#include "graphics/render_device.h"
#include "graphics/screen_fade.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/mission_failed_menu.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;
class GameplayMode;
class LevelFlowMode;

/// Game mode 0xc, the mission failed: `HUDLaunchMissionFailed(reason)` pushes it (launch()) over gameplay. It freezes
/// the game, stops the sound and shows the mission-failed menu (gui::MissionFailedMenu) over the world fading to black,
/// until a choice ends the menu; then it pops itself and acts on the choice as the pause menu does
/// (applyPauseOutcome()).
///
/// - **Enter** (`0x0015cae0`): the game stops updating (gameplay is below), the sound pauses (the original saves the
///   sound state; that it stops it is inferred), the screen fades out over 2 s (screen effect 1) and the menu opens.
/// - **Update**: the menu's frame with player 1's pad.
/// - **Render**: the world as it was, the fade, then the menu.
///
/// Coney's stand-ins (docs/research/pause.md#coneys-implementation): the game state's mission result (`+0x118` = 2)
/// is not modelled; the menu's choices are not traced, so they map onto the pause menu's outcomes
/// (gui::MissionFailedMenu); the fade is the mode's own, drawn under the menu; `HUDSetMissionFailedCallbacks`'s retry
/// function is not called.
///
/// Research: docs/research/pause.md#the-mission-failed-screen
class MissionFailedMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0xc;
    /// The title's batch.
    static constexpr std::size_t kTitleCapacity = 1;

    /// As PauseMode's constructor; every reference must outlive the mode.
    MissionFailedMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                      const gui::GlobalStrings& strings, FrontEndServices& services, GameState& state,
                      LevelFlowMode& levelFlow, GameModeStack& stack, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets the game's hooks (only pauseSound is used).
    void setHooks(PauseHooks hooks) { m_hooks = std::move(hooks); }
    /// Draws `world` (not owned; null: black) under the menu.
    void setWorld(GameplayMode* world) { m_world = world; }

    /// `HUDLaunchMissionFailed(reason)`: keeps `reason` and pushes the mode unless it is on top.
    /// @orig 0x001d1f88 MissionFailed_Launch (unknown)
    void launch(std::string_view reason);

    /// Pauses the sound, starts the fade and loads the fonts and the title; the menu opens on the next update.
    /// @orig 0x0015cae0 ModeC_Enter (unknown)
    void enter() override;
    /// The menu's frame; once it has an outcome, pops the mode and acts on it.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The world, the fade and the menu.
    void render(const RenderTime& time) override;
    /// Resumes the sound and releases the fonts.
    void exit() override;

    /// The menu.
    [[nodiscard]] const gui::MissionFailedMenu& menu() const { return m_menu; }
    /// The reason of the last launch.
    [[nodiscard]] const std::string& reason() const { return m_reason; }

  private:
    graphics::RenderDevice& m_device;
    PauseSheetLoader m_loadSheet;
    PauseRecordLoader m_loadRecord;
    const gui::GlobalStrings& m_strings;
    FrontEndServices& m_services;
    GameState& m_state;
    LevelFlowMode& m_levelFlow;
    GameModeStack& m_stack;
    std::function<void(std::string_view)> m_log;
    PauseHooks m_hooks;
    GameplayMode* m_world = nullptr;
    gui::MissionFailedMenu m_menu;
    MenuLayer m_layer;
    graphics::ScreenFade m_fade;
    Interpolated<float> m_fadeLevel{0.0F}; // the fade at the last two steps
    RenderTime m_frozen;
    std::string m_reason;
    bool m_openPending = false;
};

} // namespace coney
