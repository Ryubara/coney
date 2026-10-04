// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "gui/option_grid.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"
#include "gui/usage_info.h"

namespace coney::gui {

/// The main menu: an OptionGrid of three items with the usage line under it. The items are global strings 0x78
/// (code 0, "Story" on the English disc), 0x8a (code 5, PM_Extras; left out with the device flag 0x02) and 0x79
/// (code 1, "Quick Rumble"); the first is selected and drawn at 1.15 times the size. Up and down move the selection.
///
/// The command handler (`0x0020a268`) ignores input while the screen fade is not finished (its level is not 0).
/// **Back** returns 8 (PM_Greet) with front-end sound cue 0xf. **Accept** plays cue 9, then by the item's code: story
/// returns 0 (PM_Profile), or 6 (PM_NumPlayers) with two or more pads connected; extras returns 5; code 7 calls the
/// Lua function `Menu.reloadProfiles` (no item has it); any other code, so quick rumble, calls the Lua function the
/// profile manager was started with (`Menu.fadeToRMI`) and the menu stays.
///
/// Coney's choice: the layout is PmLayout's.
///
/// Research: docs/research/frontend.md#profile-manager
class PmMode final : public ScreenFlowState {
  public:
    /// Result of the first item (story).
    static constexpr int kStory = 0;
    /// Result of the second item (quick rumble).
    static constexpr int kQuickRumble = 1;
    /// Result of the extras item.
    static constexpr int kExtras = 5;
    /// Result that leads back to PM_Greet.
    static constexpr int kToGreet = 8;
    /// Result of story with two or more pads: PM_NumPlayers.
    static constexpr int kToNumPlayers = 6;
    /// The item code that reloads the profiles through Lua (no item of this menu has it).
    static constexpr int kReloadProfiles = 7;
    /// The Lua function code 7 calls.
    static constexpr std::string_view kReloadProfilesFunction = "Menu.reloadProfiles";
    /// The front-end sound cues of accept and back.
    static constexpr int kAcceptCue = 9;
    static constexpr int kBackCue = 0xf;
    /// The items' global strings.
    static constexpr std::uint32_t kStoryString = 0x78;
    static constexpr std::uint32_t kExtrasString = 0x8a;
    static constexpr std::uint32_t kQuickRumbleString = 0x79;

    /// A screen over `shared`, which must outlive it.
    explicit PmMode(PmShared& shared) : m_shared(shared) {}

    [[nodiscard]] std::string_view name() const override { return "PM_Mode"; }

    /// Builds the grid (the extras item only without the flag 0x02), selects the first item, takes the focus and sets
    /// the usage line.
    /// @orig 0x00209da8 PM_Mode::Init (unknown)
    void enter(ScreenFlowController& flow) override;

    /// One frame: the HUD player's menu command moves the selection or chooses an item; then draws.
    /// @orig 0x0020a4b8 PM_Mode::Update (unknown)
    /// @orig 0x0020a268 PM_Mode::HandleCommand (unknown)
    int update() override;

    /// Releases the widgets.
    void exit() override;

    /// The grid.
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }
    /// The usage line.
    [[nodiscard]] const UsageInfo& usage() const { return m_usage; }

  private:
    // Accept on the item with `code`: the cue, then the result (or a Lua call and kStay).
    int choose(int code);
    // Plays front-end sound cue `cue`, if the profile manager has a way to.
    void playSound(int cue) const;
    // Calls the Lua function `function` with no argument, if the profile manager has a way to.
    void callScript(std::string_view function) const;

    PmShared& m_shared;
    OptionGrid m_grid;
    UsageInfo m_usage;
};

} // namespace coney::gui
