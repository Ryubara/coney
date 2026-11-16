// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace coney::gui {

/// How a ScrollingWindow is set up (the `ScrollingMenu` fields the statistics screen sets).
struct ScrollingWindowSetup {
    std::size_t maxVisible = 5; ///< `+0xb0`: the most entries in the window.
    float clipHeight = 0.5F;    ///< `+0xb8`: the stacked heights past this are not drawn; 5,000 or more: no clip.
    float step = 0.25F;         ///< `+0xc8`: how far a scroll moves an entry's reveal each update.
    bool evenRows = true;       ///< `+0x100`: the middle row is found one row higher (slot `+0x1a0`).
};

/// What a ScrollingWindow is doing (`ScrollingMenu` `+0x80`).
enum class ScrollState : std::uint8_t {
    None = 0,      ///< Still: input is read.
    Up = 1,        ///< The entry above the window is being revealed at the top, the bottom one hidden.
    Down = 2,      ///< The top entry is being hidden, the one below the window revealed at the bottom.
    RevealTop = 3, ///< The entry above is being revealed because there is room under the clip.
    HideTop = 4,   ///< The top entry is being hidden because the focused entry runs past the clip.
};

/// The window of a vertical `ScrollingMenu` whose entries open under them (the statistics screen's list): which
/// entries are stacked from the list's place, how a move scrolls the window by revealing one entry while hiding
/// another, and which entries the clip lets be drawn. The cursor is the owner's (ScrollingMenu); the owner lays its
/// entries out through the callbacks.
///
/// - **Moves** (`ScrollingMenu_MoveUp` / `_MoveDown`, after the cursor moved): with half = the window's count / 2,
///   less one with evenRows (or (count − 1) / 2 without, going up), a move down past the middle row while entries are
///   left below starts Down with one more entry in the window; a move up above the window, or above the middle row
///   while entries are left above, starts Up with the window one entry higher and one longer.
/// - **Scrolling** (`ScrollingMenu_Update`), each update by `step`: Down hides the top entry (its reveal falls; at 0
/// the
///   window starts one lower) while the entry at its bottom shows 1 − that reveal; Up reveals the top entry (at 1 it is
///   done) while the bottom one shows 1 − its reveal; RevealTop reveals the top entry; HideTop hides it.
/// - **The window**: `maxVisible` entries, one more while scrolling, cut to the list's end; each placed at the stacked
///   heights of those above it, an entry's height its full height × its reveal.
/// - **The clip**: stacking the window's heights (every reveal back to 1 when still), the first entry that takes the
///   sum past `clipHeight` ends the drawn ones, itself drawn only when it is the focused one. When still, an entry
///   above the window that would fit under the clip starts RevealTop (the drawn entries fewer than `maxVisible`); a
///   focused last drawn entry that runs past the clip, when it is not the first, starts HideTop.
///
/// Research: docs/research/game-stats.md#scroll
class ScrollingWindow {
  public:
    /// What the window asks of its owner's entries each update.
    struct Entries {
        /// Places entry `index` at `offset` below the list's place (unset: where it is) with `reveal`, runs its update
        /// and returns its height at reveal 1 (the header and its shown description, `ScrollingMenuItem_GetRect`).
        std::function<float(std::size_t index, std::optional<float> offset, float reveal)> layOut;
        /// Whether entry `index` has the focus.
        std::function<bool(std::size_t index)> focused;
    };

    /// Sets the window up over `count` entries: every reveal 1, the window at the top, min(`maxVisible`, `count`)
    /// entries long, still.
    /// @orig 0x001e16f0 ScrollingMenu_Setup (unknown)
    void setup(const ScrollingWindowSetup& setup, std::size_t count);
    /// The window part of a move up to `selected`.
    /// @orig 0x001e1cb8 ScrollingMenu_MoveUp (unknown)
    void moveUp(std::size_t selected);
    /// The window part of a move down to `selected`.
    /// @orig 0x001e1d70 ScrollingMenu_MoveDown (unknown)
    void moveDown(std::size_t selected);
    /// One update: the scroll's step, the window laid out through `entries`, the clip and the scroll it starts.
    /// @orig 0x001e21d8 ScrollingMenu_Update (unknown)
    void update(const Entries& entries);

    /// The state.
    [[nodiscard]] ScrollState state() const { return m_state; }
    /// Whether a scroll runs (input waits for it).
    [[nodiscard]] bool scrolling() const { return m_state != ScrollState::None; }
    /// The first entry of the window (`+0xa4`).
    [[nodiscard]] std::size_t first() const { return m_first; }
    /// The window's length (`+0xac`).
    [[nodiscard]] std::size_t count() const { return m_count; }
    /// How many entries from first() are drawn: those the clip lets through, one more while RevealTop runs, never past
    /// the list's end.
    /// @orig 0x001e2e40 ScrollingMenu_Render (unknown)
    [[nodiscard]] std::size_t drawn() const;
    /// Entry `index`'s reveal.
    [[nodiscard]] float reveal(std::size_t index) const { return m_reveals.at(index); }

  private:
    // Sets entry `index`'s reveal, clamped to 0-1 (`ScrollingMenuItem_SetReveal`).
    // @orig 0x001e0b58 ScrollingMenuItem_SetReveal (unknown)
    void setReveal(std::size_t index, float reveal);

    ScrollingWindowSetup m_setup;
    std::vector<float> m_reveals;     // each entry's reveal (its `ScrollingMenuItem` `+0x50`)
    std::vector<float> m_heights;     // each entry's height at reveal 1 from its last lay-out
    std::size_t m_first = 0;          // `+0xa4`
    std::size_t m_count = 0;          // `+0xac`
    std::optional<std::size_t> m_fit; // `+0xb4`: the entries the clip lets through; unset: all of them
    ScrollState m_state = ScrollState::None;
};

} // namespace coney::gui
