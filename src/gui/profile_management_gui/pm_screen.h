// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gui/menu_input.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"

namespace coney::gui {

/// The front-end sound cues the profile manager's screens play (docs/research/frontend.md#audio-cues-references-only).
namespace pm_cue {
inline constexpr int kGridMove = 5;    ///< A PM grid move.
inline constexpr int kBrightness = 6;  ///< PM_Light's step.
inline constexpr int kKeyMove = 7;     ///< PM_Create's keyboard move.
inline constexpr int kAccept = 9;      ///< A PM accept.
inline constexpr int kCharacter = 0xa; ///< PM_Create: a character added.
inline constexpr int kNameOk = 0xb;    ///< PM_Create: OK on a valid name.
inline constexpr int kDelete = 0xc;    ///< PM_Create: DEL.
inline constexpr int kRefused = 0xe;   ///< A move or entry refused (end of a list, full name).
inline constexpr int kBack = 0xf;      ///< Back.
} // namespace pm_cue

/// The items of a PM screen's grid and which one is selected: the logic of the original's `OptionGrid` as the PM
/// screens use it (docs/research/frontend.md#pm-layout). Items are packed left to right in rows of the given sizes;
/// left and right move within a row, up and down change row keeping the column (clamped to the row's length). A move
/// that cannot leave its item is refused. Nothing wraps (PM_Mode's wrap is its own).
class PmChoices {
  public:
    /// One item: its text and the code accept returns.
    struct Item {
        std::string text;
        int code = 0;
    };

    /// Replaces the items with `items` in rows of `rowSizes` (their sum must be items.size(), CONEY_ASSERT; empty
    /// means one item a row) and selects item `selected` (clamped).
    void set(std::vector<Item> items, std::vector<std::size_t> rowSizes = {}, std::size_t selected = 0);
    /// Acts on a direction: returns true when the selection moved, false when it could not (or for accept and back).
    bool move(MenuCommand command);
    /// Selects item `index` (clamped).
    void select(std::size_t index);

    /// The items.
    [[nodiscard]] const std::vector<Item>& items() const { return m_items; }
    /// The row sizes.
    [[nodiscard]] const std::vector<std::size_t>& rows() const { return m_rows; }
    /// The selected item's index.
    [[nodiscard]] std::size_t selected() const { return m_selected; }
    /// The selected item's code; nothing for an empty grid.
    [[nodiscard]] std::optional<int> selectedCode() const;
    /// The row and column of item `index`.
    [[nodiscard]] std::pair<std::size_t, std::size_t> cell(std::size_t index) const;

  private:
    // The index of the item at `row`, `column` (the column clamped to the row).
    [[nodiscard]] std::size_t indexOf(std::size_t row, std::size_t column) const;

    std::vector<Item> m_items;
    std::vector<std::size_t> m_rows;
    std::size_t m_selected = 0;
};

/// The base of the profile manager's story screens: each frame it reads the HUD player's menu command (none while
/// the screen fade is running or not clear), hands it to handle(), which holds the screen's logic, and then calls
/// draw(), which only shows the state the logic left. A screen's look lives in its draw() and enter's widget set-up,
/// so restyling a screen does not touch its logic.
///
/// Research: docs/research/frontend.md#pm-screens
class PmScreen : public ScreenFlowState {
  public:
    /// Takes the input focus and calls open().
    void enter(ScreenFlowController& flow) final;
    /// One frame: poll(), then the menu command into handle(), then draw(). Returns the first result that is not kStay.
    int update() final;
    /// Calls close().
    void exit() final { close(); }

    /// The screen's logic for one menu command: a result (kStay, kBack or a transition code). Public for tests.
    virtual int handle(MenuCommand command) = 0;

  protected:
    /// A screen over `shared`, which must outlive it.
    explicit PmScreen(PmShared& shared) : m_shared(shared) {}

    /// The screen's `Init`: its state and widgets.
    virtual void open() = 0;
    /// Releases the widgets.
    virtual void close() = 0;
    /// Input other than menu commands, before them (PM_NumPlayers' second pad); returns a result or kStay.
    virtual int poll() { return kStay; }
    /// Shows the state the logic left; called every frame after the input.
    virtual void draw() = 0;

    /// Whether input waits for the screen fade (running, or its level not 0).
    [[nodiscard]] bool faded() const;
    /// Plays front-end sound cue `cue`, if the profile manager has a way to.
    void playSound(int cue) const;
    /// Calls the Lua function `function` with `args`, if the profile manager has a way to.
    void callScript(std::string_view function, std::span<const double> args = {}) const;
    /// A move through `choices`: cue 5 when it moved, 0xe when it could not. Returns kStay.
    int moveIn(PmChoices& choices, MenuCommand command) const;

    PmShared& m_shared;
};

} // namespace coney::gui
