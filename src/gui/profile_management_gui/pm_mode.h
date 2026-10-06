// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "gui/option_grid.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"
#include "gui/usage_info.h"

namespace coney::gui {

/// The main menu: a PM grid at (x, the two-row grid y 0.76) of rows {2, 1}, "STORY : EXTRAS" over "QUICK RUMBLE"
/// (global strings 0x78 with the separator, code 0; 0x8a, code 5; 0x79, code 1), and the usage line. With the device
/// flag 0x02 one row {2} of STORY and QUICK RUMBLE at the one-row y 0.81. STORY is selected on entry, so re-entering
/// the screen brings the default back.
///
/// Left and right walk STORY → EXTRAS → QUICK RUMBLE with wrap; up and down switch rows keeping the column (clamped);
/// a move plays cue 5, a refused one 0xe (the grid's rules, gui::OptionGrid).
///
/// The command handler (`0x0020a268`) ignores input while the screen fade is not clear. **Back** returns 8 (PM_Greet)
/// with cue 0xf. **Accept** plays cue 9, then by the item's code: story returns 0 (PM_Profile), or 6 (PM_NumPlayers)
/// with two or more pads connected; extras returns 5; code 7 calls the Lua function `Menu.reloadProfiles` (no item has
/// it); any other code, so quick rumble, calls the Lua function the profile manager was started with
/// (`Menu.fadeToRMI`) and the menu stays.
///
/// Research: docs/research/frontend.md#pm-screens
class PmMode final : public ScreenFlowState {
  public:
    /// Result of the first item (story).
    static constexpr int kStory = 0;
    /// Code of the quick rumble item.
    static constexpr int kQuickRumble = 1;
    /// Result of the extras item.
    static constexpr int kExtras = 5;
    /// Result of story with two or more pads: PM_NumPlayers.
    static constexpr int kToNumPlayers = 6;
    /// The item code that reloads the profiles through Lua (no item of this menu has it).
    static constexpr int kReloadProfiles = 7;
    /// Result that leads back to PM_Greet.
    static constexpr int kToGreet = 8;
    /// The Lua function code 7 calls.
    static constexpr std::string_view kReloadProfilesFunction = "Menu.reloadProfiles";
    /// The items' global strings.
    static constexpr std::uint32_t kStoryString = 0x78;
    static constexpr std::uint32_t kExtrasString = 0x8a;
    static constexpr std::uint32_t kQuickRumbleString = 0x79;

    /// A screen over `shared`, which must outlive it.
    explicit PmMode(PmShared& shared) : m_shared(shared) {}

    [[nodiscard]] std::string_view name() const override { return "PM_Mode"; }

    /// Builds the grid and the usage line and takes the focus.
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

    PmShared& m_shared;
    OptionGrid m_grid;
    UsageInfo m_usage;
};

} // namespace coney::gui
