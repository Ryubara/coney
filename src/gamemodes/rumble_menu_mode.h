// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;

/// The level number of the arena the placeholder Rumble menu picks: the Fight Pen, `level102`.
inline constexpr int kDefaultRumbleArena = 102;

/// The set-up the placeholder Rumble menu leaves for the arena (**Coney's choice**: the menu's screens are not
/// researched, so which of the 23 values each sets is open): a brawl in `level102` with the values listed in
/// rumble_menu_mode.cpp, which give the arena script a gang of Warriors for player 1.
[[nodiscard]] RumbleSetup defaultRumbleSetup();

/// defaultRumbleSetup() for the arena `level` when `level` names a Rumble arena (`level101` to `level137`), with that
/// arena's level number; nothing for any other level. For `--play-level` of an arena, which has no menu before it.
[[nodiscard]] std::optional<RumbleSetup> rumbleSetupForLevel(std::string_view level);

/// Game mode 0x11, the Rumble set-up menu that QUICK RUMBLE opens (`ShowRumbleModeInterface`): it keeps the cancel and
/// start callbacks, runs the menu's screens, which write the set-up the arena reads (`GetRumbleModeData`) and the
/// chosen arena's level number, and on leaving calls the cancel callback or the start one with that level number
/// (docs/research/frontend.md#quick-rumble).
///
/// Coney's stand-ins: the screens (`RumbleModeGUI/`) are not researched, so the mode is one placeholder screen on
/// black that fills defaultRumbleSetup() on entry; cross (on release, as a menu accepts) starts the fight, triangle or
/// circle backs out. When it starts a fight, the profile manager below it (mode 0x12) is popped too, without its start
/// game callback, as its controller ends with Rumble mode chosen (docs/research/frontend.md#mode-flow); the level flow
/// on top then starts the arena. No autosave (Coney has no saves).
class RumbleMenuMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x11;

    /// Draws through `device`, pushes itself on `stack`, calls the callbacks in `scripts` and writes the set-up into
    /// `state`; each must outlive the mode. `log` gets a line for each step.
    RumbleMenuMode(graphics::RenderDevice& device, GameModeStack& stack, script::ScriptSystem& scripts,
                   GameState& state, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `ShowRumbleModeInterface(onCancel, onStart, fromFrontEnd)`: keeps the two callbacks and whether the menu was
    /// opened from the front end, and pushes the mode unless it is on top.
    /// @orig 0x00155228 RumbleMenu_Show (unknown)
    void show(std::string onCancel, std::string onStart, bool fromFrontEnd);

    /// Clears the outcome and fills the placeholder's set-up.
    /// @orig 0x0015e8b0 Mode11::Enter (unknown)
    void enter() override;

    /// One step: cross starts, triangle or circle cancels; either leaves.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Black, where the menu's screens would be.
    void render(const RenderTime& time) override;

    /// Calls the cancel callback when cancelled, else the start callback with the arena's level number when started.
    /// @orig 0x0015ea40 Mode11::Exit (unknown)
    void exit() override;

    /// Whether the last visit started a fight / was cancelled.
    [[nodiscard]] bool started() const { return m_started; }
    [[nodiscard]] bool cancelled() const { return m_cancelled; }
    /// Whether the menu was opened from the front end (`0x0063ef64`).
    [[nodiscard]] bool fromFrontEnd() const { return m_fromFrontEnd; }

  private:
    graphics::RenderDevice& m_device;
    GameModeStack& m_stack;
    script::ScriptSystem& m_scripts;
    GameState& m_state;
    std::function<void(std::string_view)> m_log;
    std::string m_onCancel; // `0x005e67c0`
    std::string m_onStart;  // `0x005e67c4`
    bool m_fromFrontEnd = false;
    bool m_started = false;   // `0x0050f4e0`
    bool m_cancelled = false; // `0x0050f4dc`
};

} // namespace coney
