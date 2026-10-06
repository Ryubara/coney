// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "gui/profile_management_gui/pm_look.h"
#include "gui/profile_management_gui/pm_screen.h"
#include "gui/text_widget.h"

namespace coney::gui {

/// A PM screen made of an optional title, a grid and the usage line (PM_NumPlayers, PM_Profile, PM_Difficulty,
/// PM_Subtitles, PM_Load, PM_Continue, PM_Delete). Its logic is the grid's: a direction moves the selection (cue 5, or
/// `0xe` when it cannot), accept plays cue 9 and hands the selected item's code to accept(), back plays `0xf` and pops.
///
/// Research: docs/research/frontend.md#pm-screens, docs/research/frontend.md#pm-layout
class PmListScreen : public PmScreen {
  public:
    int handle(MenuCommand command) override;

    /// The grid's items and selection.
    [[nodiscard]] const PmChoices& choices() const { return m_choices; }
    /// The title's text (empty for none).
    [[nodiscard]] const std::string& title() const { return m_title; }

  protected:
    using PmScreen::PmScreen;

    /// Fills the items (and the title, and the y positions when they are not the row count's) for this entry.
    virtual void setUp() = 0;
    /// Accept on the item with `code`, after cue 9: a result.
    virtual int accept(int code) = 0;
    /// A direction: moves the selection by default.
    virtual int direction(MenuCommand command) { return moveIn(m_choices, command); }
    /// Draws a screen's own widgets after the common ones.
    virtual void drawExtras() {}
    /// Releases a screen's own widgets.
    virtual void closeExtras() {}

    void open() final;
    void close() final;
    void draw() final;

    PmChoices m_choices;
    std::string m_title;           ///< Set by setUp(); empty: no title.
    std::optional<float> m_titleY; ///< Set by setUp() to override pm_look::titleY().
    std::optional<float> m_gridY;  ///< Set by setUp() to override pm_look::gridY().

  private:
    TextWidget m_titleText;
    TextWidget m_usage;
    PmGridView m_grid;
};

} // namespace coney::gui
