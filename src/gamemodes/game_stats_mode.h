// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/pause_mode.h"
#include "graphics/render_device.h"
#include "graphics/screen_fade.h"
#include "gui/game_stats.h"
#include "gui/global_strings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;

/// Game mode 0x13, the statistics screen: `ShowGameStatsInterface(onClose)` pushes it (launch()). Its enter loads the
/// `menu` sound bank unless it is in sound RAM and the screen's sheets, and opens the screen (gui::GameStatsScreen)
/// over the game state's level table, unlockables and the profile's bests; the screen fades in (kFadeSeconds) on the
/// frame it is ready. Its update runs the screen with player 1's pad; back starts a fade out, and the mode pops on the
/// first frame the fade is over, its exit calling the Lua function `onClose`.
///
/// Coney's stand-ins: the original's update also runs one world frame with the task manager in phase 0 and its enter
/// sets the store tint (`ScreenFx_EnterStore`) with a clear colour, neither of which shows under the screen's opaque
/// background; Coney updates no world and draws the screen over black.
///
/// Research: docs/research/game-stats.md#mode
class GameStatsMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x13;
    /// The batches' capacities: the headers' backs and circles (three sprites for each of the 26 listed levels) and the
    /// special pictures; the pictures.
    static constexpr std::size_t kHeaderCapacity = 96;
    static constexpr std::size_t kThumbCapacity = 12;
    /// The sound bank the enter makes current (`0x0054ee50`).
    static constexpr std::string_view kSoundBank = "menu";
    /// The fades' length in seconds, in and out (`ScreenFx_Queue(1.0, ...)`).
    static constexpr double kFadeSeconds = 1.0;

    /// Draws through `device` with sheets from `loadSheet` and `loadRecord`, strings from `strings`, banks and cues
    /// through `services`, fading through `fade`, reading `state` and calling `onClose` through `scripts`. Every
    /// reference must outlive the mode.
    /// @coney-only Coney's wiring of the mode's services
    GameStatsMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                  const gui::GlobalStrings& strings, FrontEndServices& services, graphics::ScreenFade& fade,
                  GameState& state, script::ScriptSystem& scripts, GameModeStack& stack,
                  std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `ShowGameStatsInterface(onClose)`: keeps the callback's name and pushes the mode unless it is on top.
    /// @coney-only the binding's push, as Coney's mode stack takes it (the binding is ported in scripting)
    void launch(std::string_view onClose);

    /// Makes the `menu` bank current and loads the sheets; the screen opens on the next update.
    /// @orig 0x00159538 GameStatsMode_Enter (unknown)
    void enter() override;
    /// The screen's frame: its fade in once it is ready; once back closes it, a fade out, then the pop.
    /// @orig 0x001596d0 GameStats_Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The screen over black, then the fade.
    /// @coney-only Coney's split of the frame: the original draws in its update
    void render(const RenderTime& time) override;
    /// Shuts the screen, releases the sheets and, when back closed it, calls `onClose`.
    /// @coney-only the mode's exit as Coney's stack calls it (mode 0x13's exit is not traced)
    void exit() override;

    /// The screen.
    [[nodiscard]] const gui::GameStatsScreen& screen() const { return m_screen; }
    /// The callback's name of the last launch.
    [[nodiscard]] const std::string& onClose() const { return m_onClose; }

  private:
    graphics::RenderDevice& m_device;
    PauseSheetLoader m_loadSheet;
    PauseRecordLoader m_loadRecord;
    const gui::GlobalStrings& m_strings;
    FrontEndServices& m_services;
    graphics::ScreenFade& m_fade;
    GameState& m_state;
    script::ScriptSystem& m_scripts;
    GameModeStack& m_stack;
    std::function<void(std::string_view)> m_log;
    gui::GameStatsScreen m_screen;
    MenuLayer m_layer;
    std::string m_onClose;
    bool m_openPending = false;
    bool m_fadeInPending = false; // the fade in waits for the frame the screen is ready (`+0xd4`)
    bool m_fadingOut = false;     // back has queued the fade out (`+0xd0`)
};

} // namespace coney
