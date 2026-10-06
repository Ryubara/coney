// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/pause_mode.h"
#include "graphics/render_device.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/pause_menu.h"
#include "gui/rumble_mode_gui/rumble_result_menu.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;
class GameplayMode;
class LevelFlowMode;

/// The pause menu's outcome a Rumble result choice acts as: Replay restarts the level from checkpoint 1; Rumble menu
/// calls `PauseGoToRMIQuick` from the front end, else `PauseGoToRMIHangout`; Quit goes to the main menu from the front
/// end, else `runNextMission(0)`, the hangout (docs/research/rumble.md#result-screen).
[[nodiscard]] gui::PauseOutcome rumbleResultOutcome(gui::RumbleResultChoice choice, bool fromFrontEnd);

/// Game mode 0x14, the Rumble result: `HUDLaunchRumbleWin(winner, reason)` pushes it (launch()) over gameplay at the
/// end of a match. Unlike the pause, the world keeps updating and drawing under the screen (the win camera circling the
/// winner); the screen (gui::RumbleResultMenu) updates and draws on top. Once a choice ends the screen, the mode pops
/// itself and acts on it through the pause menu's outcomes (rumbleResultOutcome(), applyPauseOutcome()).
///
/// Coney's stand-ins: the world's update is gameplay's frame of play without START's pause check (GameplayMode::
/// updateWorld()); the screen reads player 1's pad; the level-end state 3 is applyPauseOutcome()'s pop of gameplay; the
/// sound call when the choices appear (`0x0010fa50`, open question) is not made.
///
/// Research: docs/research/rumble.md#result-screen
class RumbleResultMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x14;

    /// Draws through `device` with fonts from `loadSheet`, strings from `strings`, cues through `services`, acting on
    /// `state`, `levelFlow` and `stack`; `fromFrontEnd` says whether the Rumble was started from the front end
    /// (`0x0063ef64`, the Rumble menu's). Every reference must outlive the mode.
    RumbleResultMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, const gui::GlobalStrings& strings,
                     FrontEndServices& services, GameState& state, LevelFlowMode& levelFlow, GameModeStack& stack,
                     std::function<bool()> fromFrontEnd, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Updates and draws `world` (not owned; null: black) under the screen.
    void setWorld(GameplayMode* world) { m_world = world; }

    /// `HUDLaunchRumbleWin(winner, reason)`: keeps the two texts and pushes the mode unless it is on top.
    /// @orig 0x001dfe20 RumbleWin_Launch (unknown)
    void launch(std::string_view winner, std::string_view reason);

    /// Loads the fonts; the screen opens on the next update.
    /// @orig 0x0015f1a0 Mode14_Enter (unknown)
    void enter() override;
    /// The world's frame, then the screen's; once it has a choice, pops the mode and acts on it.
    /// @orig 0x0015f308 Mode14_Update (unknown)
    /// @orig 0x00155648 RumbleWin_Choose (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The world with the screen over it.
    void render(const RenderTime& time) override;
    /// Hides the screen and releases the fonts.
    /// @orig 0x0015f248 Mode14_Exit (unknown)
    void exit() override;

    /// The screen.
    [[nodiscard]] const gui::RumbleResultMenu& menu() const { return m_menu; }
    /// The texts of the last launch.
    [[nodiscard]] const std::string& winner() const { return m_winner; }
    [[nodiscard]] const std::string& reason() const { return m_reason; }

  private:
    graphics::RenderDevice& m_device;
    PauseSheetLoader m_loadSheet;
    const gui::GlobalStrings& m_strings;
    FrontEndServices& m_services;
    GameState& m_state;
    LevelFlowMode& m_levelFlow;
    GameModeStack& m_stack;
    std::function<bool()> m_fromFrontEnd;
    std::function<void(std::string_view)> m_log;
    GameplayMode* m_world = nullptr;
    gui::RumbleResultMenu m_menu;
    MenuLayer m_layer;
    std::string m_winner;
    std::string m_reason;
    bool m_openPending = false;
};

} // namespace coney
