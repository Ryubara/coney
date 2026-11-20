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
#include "gui/global_strings.h"
#include "gui/mission_select.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;

/// Game mode 0x10, the mission select: `HUDShowMissionSelect(onCancel, onChoose)` pushes it (launch()), as the hub's
/// subway menu does (`menu.DoSubwayMenu`, `level95.lua`). Its enter clears the choice, loads the `menu` sound bank
/// unless it is current and opens the screen (gui::MissionSelectScreen) over the level table and the unlockables. Its
/// update runs the screen with player 1's pad; the screen fades in (kFadeSeconds) on the frame it is ready. Once the
/// player backs out the screen fades out over kFadeSeconds; once he chooses, the music stops and it fades out over
/// kChooseFadeSeconds. The mode pops on the first frame the fade is over, its exit calling `onCancel()` (after loading
/// the `sound` bank) or `onChoose(level, checkpoint)`.
///
/// The choice from the second checkpoint on also clears the inventories (their callbacks and counts) and both players'
/// statistics, and takes their checkpoint copies (`MissionSelect_OnAccept`).
///
/// Coney's stand-ins: the original's update also runs one world frame (the task manager's phase 0, the managers, the
/// world and the audio from the player's camera), unseen under the opaque map; Coney updates no world, as no menu mode
/// runs one yet. The original's update steps the game timer at a fixed step, which Coney's always does.
///
/// Research: docs/research/frontend.md#fn-missionselecthud
class MissionSelectMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0x10;
    /// The batches' capacities: the frame sheet (the map, the halo, the markers, the title's and entries' backs and
    /// circles) and the logos (`MissionSelect_CreateFrameBatch` 40 and `_CreateMapBatch` 50 over the same record, as
    /// one; `_CreateLogoBatch` 2).
    static constexpr std::size_t kFrameCapacity = 90;
    static constexpr std::size_t kLogoCapacity = 2;
    /// The batches' depths.
    static constexpr float kFrameDepth = 8000.0F;
    static constexpr float kLogoDepth = 8500.0F;
    /// The sound bank the enter makes current (`0x0054f680`), and the one a cancel loads (`0x0054f688`).
    static constexpr std::string_view kSoundBank = "menu";
    static constexpr std::string_view kCancelBank = "sound";
    /// The fades in seconds: in, out after a cancel (`ScreenFx_Queue(1.0, ...)`), out after a choice (1.5).
    static constexpr double kFadeSeconds = 1.0;
    static constexpr double kChooseFadeSeconds = 1.5;

    /// Draws through `device` with sheets from `loadSheet` and `loadRecord`, strings from `strings`, banks, cues and
    /// music through `services`, fading through `fade`, reading and changing `state` and calling the callbacks through
    /// `scripts`, the screen laid out for the video mode (`europe`: the device's flag `0x02`; Coney's screen is never
    /// 16:9 or progressive here). Every reference must outlive the mode.
    /// @coney-only Coney's wiring of the mode's services
    MissionSelectMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                      const gui::GlobalStrings& strings, FrontEndServices& services, graphics::ScreenFade& fade,
                      GameState& state, script::ScriptSystem& scripts, GameModeStack& stack, bool europe,
                      std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// `HUDShowMissionSelect(onCancel, onChoose)`: keeps the callbacks' names and pushes the mode unless it is on top.
    /// @orig 0x00155180 MissionSelect_Show (unknown)
    void launch(std::string_view onCancel, std::string_view onChoose);

    /// Clears the choice, makes the `menu` bank current, loads the sheets and opens the screen.
    /// @orig 0x0015d648 MissionSelect_Enter (unknown)
    void enter() override;
    /// The screen's frame: its fade in once it is ready; once it is done, the fade out (and the music stopped after a
    /// choice), then the pop when the fade is over.
    /// @orig 0x0015d890 MissionSelect_Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The map over black, then the fade.
    /// @coney-only Coney's split of the frame: the original draws in its update
    void render(const RenderTime& time) override;
    /// Shuts the screen and calls `onCancel` or `onChoose(level, checkpoint)`.
    /// @orig 0x0015d728 MissionSelect_Exit (unknown)
    void exit() override;

    /// The screen.
    [[nodiscard]] const gui::MissionSelectScreen& screen() const { return m_screen; }

  private:
    // The choice's effect on the players from the second checkpoint on: the inventories and statistics cleared.
    // @orig 0x001bdd38 MissionSelect_OnAccept (MissionSelectHUD.cpp)
    void resetPlayersForChoice();

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
    gui::MissionSelectScreen m_screen;
    MenuLayer m_layer;
    std::string m_onCancel;       // `+0x6d8`
    std::string m_onChoose;       // `+0x6f8`
    bool m_openPending = false;   // the screen opens on the first update, with its time
    bool m_fadeInPending = false; // `+0x6d4`: the fade in waits for the frame the screen is ready
    bool m_fadingOut = false;     // `+0x6d0`: the fade out is queued
    bool m_choiceApplied = false; // the choice's effect on the players is done
};

} // namespace coney
