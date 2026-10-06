// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "gui/menu_input.h"

namespace coney::gui {

/// How a ScrollingMenu is set up.
struct ScrollingMenuSetup {
    std::size_t visible = 3;                ///< How many entries show at once.
    int moveCue = 4;                        ///< The front-end sound cue of a move.
    std::function<void(int cue)> playCue{}; ///< Plays a front-end sound cue; may be empty.
};

/// A vertical list of entries with a cursor, of which a window of `visible` entries is shown: the Rumble menu's Game
/// Mode and Choose Area lists. It keeps the cursor and the window; its owner draws the entries.
///
/// - **Moves:** up and down move the cursor one entry with moveCue; **at either end the cursor stays**, plays
///   kRefusedCue and switches the d-pad to its plain (not repeating) query until the next move. Left and right do
///   nothing. Moves less than kInputGapMs after the last one are ignored.
/// - **Window:** the first visible entry keeps the cursor in the middle row where it can (clamped to the list's ends).
///
/// Coney's choice where the page is silent: the window jumps to its new place; the original animates the scroll with a
/// reveal stepped 0.25 a frame (`+0xc8`), not traced further.
///
/// Research: docs/research/frontend.md#rm-layout
/// @orig 0x001e1338 ScrollingMenu::ScrollingMenu (unknown)
class ScrollingMenu {
  public:
    /// The cue of a move at either end.
    static constexpr int kRefusedCue = 0xe;
    /// The least time between two moves, in milliseconds (`+0xd0`).
    static constexpr std::uint64_t kInputGapMs = 100;

    /// Sets the menu up over `count` entries, the first selected.
    void setup(ScrollingMenuSetup setup, std::size_t count);
    /// Selects entry `index` (clamped to the list) without a sound.
    void select(std::size_t index);
    /// Takes the input focus at `nowMs`: `input` (which must outlive the menu's use of it) forgets its last command,
    /// with the d-pad repeating.
    void takeFocus(MenuInput& input, std::uint64_t nowMs);

    /// Acts on one menu command at `nowMs`: the moves above. Accept returns the selected entry; nothing for every other
    /// command, and for an empty list.
    /// @orig 0x001e1e48 ScrollingMenu_HandleCommand (unknown)
    [[nodiscard]] std::optional<std::size_t> handle(MenuCommand command, std::uint64_t nowMs);

    /// How many entries there are.
    [[nodiscard]] std::size_t count() const { return m_count; }
    /// The selected entry.
    [[nodiscard]] std::size_t selected() const { return m_selected; }
    /// The first entry shown.
    [[nodiscard]] std::size_t firstVisible() const;
    /// How many entries are shown: the window's size, or the list's when shorter.
    [[nodiscard]] std::size_t visibleCount() const;

  private:
    // Moves the cursor by `step` (-1 or 1), or refuses at an end.
    void move(int step);
    // Plays `cue`, if the menu has a way to.
    void playCue(int cue) const;

    ScrollingMenuSetup m_setup;
    std::size_t m_count = 0;
    std::size_t m_selected = 0;
    std::optional<std::uint64_t> m_lastMoveMs;
    MenuInput* m_input = nullptr;
};

} // namespace coney::gui
