// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// The tagging panel's `part_page0` rectangles: the path's dots, the painted cells, the cursor, the charge bar and the
/// frame.
inline constexpr std::size_t kTagPathRect = 64;
inline constexpr std::size_t kTagCellRect = 62;
inline constexpr std::size_t kTagCursorRect = 63;
inline constexpr std::size_t kTagBarRect = 78;
inline constexpr std::size_t kTagFrameRect = 93;
/// The panel's sizes in overlay units (square on screen), default video mode: one grid cell's step, the path's dots,
/// the painted cells, the cursor, the frame, and the charge bar's width and full height.
inline constexpr float kTagCellStep = 0.0025F;
inline constexpr float kTagPathSize = 0.015F;
inline constexpr float kTagCellSize = 0.03F;
inline constexpr float kTagCursorSize = 0.04F;
inline constexpr float kTagFrameSize = 0.07F;
inline constexpr float kTagBarWidth = 0.0255F;
inline constexpr float kTagBarHeight = 0.045F;
/// The frame's offset from the origin, and the charge bar's bottom middle (overlay units, y up).
inline constexpr float kTagFrameX = 0.01F;
inline constexpr float kTagFrameY = 0.07F;
inline constexpr float kTagBarX = 0.0083F;
inline constexpr float kTagBarBottom = 0.045F;
/// The path's last points are not drawn.
inline constexpr std::size_t kTagPathHiddenEnd = 3;
/// The painted cells' alpha and the frame's colour.
inline constexpr std::uint8_t kTagCellAlpha = 205;
inline constexpr graphics::Rgba kTagFrameColour{191, 191, 191, 191};
/// The cursor fades from white to black and back, this long each way.
inline constexpr std::uint64_t kTagCursorFadeMs = 250;

/// One cell of the tagging grid (256 × 256), as the panel draws it.
struct TagPanelCell {
    int x = 0;
    int y = 0;
};

/// What the panel shows of a player's tag game this update.
struct TagPanelState {
    std::vector<TagPanelCell> path;    ///< The pattern's path points.
    std::vector<TagPanelCell> painted; ///< The cells painted so far.
    float cursorX = 0.0F;              ///< The cursor, in grid cells (not snapped to a cell).
    float cursorY = 0.0F;
    float chargeLeft = 1.0F; ///< The share of the current charge of paint left, 0-1.
    bool paused = false;     ///< The game is paused after a slip or a spent charge.
    graphics::Rgba colour;   ///< The tagger's paint colour (`HuTagColor`).
};

/// One player's tagging panel: the pattern's path as grey dots darker at the start, the painted cells in the paint's
/// colour, the cursor fading white to black, the charge of paint as a bar growing up from its bottom, and a frame.
/// The play mode sets it each update while the player has a tag game and clears it when the game ends.
///
/// Research: docs/research/hud.md#fn-after-subtitle
/// @orig 0x001cb6f0 TagHud_Setup (unknown)
class TagHud {
  public:
    /// This update's game, or none to hide the panel.
    void set(std::optional<TagPanelState> state) { m_state = std::move(state); }
    [[nodiscard]] bool shown() const { return m_state.has_value(); }
    [[nodiscard]] const std::optional<TagPanelState>& state() const { return m_state; }

    /// The panel's origin for player `player` in the overlay camera's space: GUI (0.01, 0.70) or (0.76, 0.70).
    [[nodiscard]] static graphics::OverlayPoint origin(std::size_t player);
    /// The cursor's colour at game time `nowMs`: white, fading to black over 250 ms and back over the next 250 ms;
    /// white while `paused`.
    [[nodiscard]] static graphics::Rgba cursorColour(std::uint64_t nowMs, bool paused);

    /// Adds player `player`'s panel to `canvas.parts` while it is shown: the path, the painted cells, the cursor, the
    /// charge bar and the frame.
    /// @orig 0x001cb798 TagHud_Render (unknown)
    void render(const HudCanvas& canvas, std::size_t player, std::uint64_t nowMs) const;

  private:
    std::optional<TagPanelState> m_state;
};

} // namespace coney::hud
