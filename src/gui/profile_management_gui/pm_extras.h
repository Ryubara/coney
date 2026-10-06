// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "gui/option_grid.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"
#include "gui/usage_info.h"

namespace coney::gui {

/// EXTRAS: a PM grid at (x, the one-row y 0.81) of one item, global string 0x8c TRAILER (code 0), and the usage line.
/// Accept calls the Lua function `Menu.playMovie(1)` (the `TRAILER` movie) with cue 9 and stays; back pops with cue
/// 0xf. Both are ignored while the screen fade is not clear.
///
/// Research: docs/research/frontend.md#pm-screens
class PmExtras final : public ScreenFlowState {
  public:
    /// The trailer item's global string and code.
    static constexpr std::uint32_t kTrailerString = 0x8c;
    static constexpr int kTrailer = 0;
    /// The Lua function accept calls, and its argument: entry 1 of `Menu.movies`, `TRAILER`.
    static constexpr std::string_view kPlayMovieFunction = "Menu.playMovie";
    static constexpr double kTrailerMovie = 1.0;

    /// A screen over `shared`, which must outlive it.
    explicit PmExtras(PmShared& shared) : m_shared(shared) {}

    [[nodiscard]] std::string_view name() const override { return "PM_Extras"; }

    /// Builds the grid and the usage line and takes the focus.
    /// @orig 0x002071d8 PM_Extras::Init (unknown)
    void enter(ScreenFlowController& flow) override;

    /// One frame: accept plays the trailer, back pops; then draws.
    /// @orig 0x002074f8 PM_Extras::HandleCommand (unknown)
    int update() override;

    /// Releases the widgets.
    void exit() override;

    /// The grid.
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }

  private:
    PmShared& m_shared;
    OptionGrid m_grid;
    UsageInfo m_usage;
};

} // namespace coney::gui
