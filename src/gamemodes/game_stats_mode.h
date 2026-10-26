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
#include "gui/game_stats.h"
#include "gui/global_strings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;

/// Game mode 0x13, the statistics screen: `ShowGameStatsInterface(onClose)` pushes it (launch()). Its enter loads the
/// screen's sheets and opens it (gui::GameStatsScreen) over the game state's level table, unlockables and the profile's
/// bests; its update runs the screen with player 1's pad, with no world update; back pops the mode, whose exit calls
/// the Lua function `onClose`.
///
/// Coney's choices: the screen is drawn over black (its background covers the screen); the sound is left as it is
/// (the page does not say the mode touches it).
///
/// Research: docs/research/frontend.md#game-stats
class GameStatsMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x13;
    /// The batches' capacities: the headers' backs and circles (three sprites for each of the 26 listed levels) and the
    /// special pictures; the pictures.
    static constexpr std::size_t kHeaderCapacity = 96;
    static constexpr std::size_t kThumbCapacity = 12;

    /// Draws through `device` with sheets from `loadSheet` and `loadRecord`, strings from `strings`, cues through
    /// `services`, reading `state` and calling `onClose` through `scripts`. Every reference must outlive the mode.
    GameStatsMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                  const gui::GlobalStrings& strings, FrontEndServices& services, GameState& state,
                  script::ScriptSystem& scripts, GameModeStack& stack, std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `ShowGameStatsInterface(onClose)`: keeps the callback's name and pushes the mode unless it is on top.
    void launch(std::string_view onClose);

    /// Loads the sheets; the screen opens on the next update.
    /// @orig 0x00159538 GameStatsMode_Enter (unknown)
    void enter() override;
    /// The screen's frame; once back closes it, pops the mode.
    /// @orig 0x001596d0 GameStats_Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The screen over black.
    void render(const RenderTime& time) override;
    /// Shuts the screen, releases the sheets and, when back closed it, calls `onClose`.
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
    GameState& m_state;
    script::ScriptSystem& m_scripts;
    GameModeStack& m_stack;
    std::function<void(std::string_view)> m_log;
    gui::GameStatsScreen m_screen;
    MenuLayer m_layer;
    std::string m_onClose;
    bool m_openPending = false;
};

} // namespace coney
