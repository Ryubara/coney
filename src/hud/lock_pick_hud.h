// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// The dial's pins (`hud_minigames` rectangle 11, the brass lock cylinder) and its centre disc (rectangle 12, the lock
/// face): the pins' first size (overlay units) and shrink step, and their grey.
inline constexpr std::size_t kLockPinRect = 11;
inline constexpr std::size_t kLockFaceRect = 12;
inline constexpr std::size_t kLockPinCount = 3;
inline constexpr float kLockPinSize = 0.19F;
inline constexpr float kLockPinShrink = 0.2F;
inline constexpr graphics::Rgba kLockPinColour{191, 191, 191, 255};
/// The shapes' radii in 640 × 448 screen pixels (Coney's logical pixels): the two wedges and the centre disc.
inline constexpr float kLockBandRadius = 40.0F;
inline constexpr float kLockFaceRadius = 41.0F;
/// The wedges' colours: band 1 (the good zone) and band 2 (the perfect one), both alpha 100.
inline constexpr graphics::Rgba kLockGoodColour{191, 16, 16, 100};
inline constexpr graphics::Rgba kLockPerfectColour{50, 7, 7, 100};
/// The wedges' shares of the circle, per cent, by difficulty 0-2.
inline constexpr std::array<float, 3> kLockGoodPercent{35.0F, 27.0F, 20.0F};
inline constexpr std::array<float, 3> kLockPerfectPercent{14.0F, 12.0F, 9.2F};
/// The shapes' circles are drawn in 32 segments.
inline constexpr int kLockSegments = 32;

/// One player's lock-pick dial (HUD `+0xf420` + player × `0x540`): three pins of shrinking size turning about one
/// centre, each by its own angle, with two red wedges at 12 o'clock (the good and the perfect zone) and the lock face
/// drawn over them. The play mode shows it while the player picks a lock and gives it the pins' angles each update;
/// there is no text.
///
/// Research: docs/research/hud.md#lock-pick-dial-layout
/// @orig 0x001b7eb0 LockPickDial_Init (unknown)
class LockPickHud {
  public:
    /// Shows the dial at `difficulty` (0-2, the wedges' sizes) with every pin at π, or hides it.
    /// @orig 0x001b8428 LockPickDial_Show (unknown)
    void show(bool on, int difficulty = 0);
    [[nodiscard]] bool shown() const { return m_shown; }
    /// This update's pin angles (radians, clockwise on screen) and the pin the next press judges (3 or more when none
    /// is left, which hides the pins).
    void setPins(const std::array<float, kLockPinCount>& angles, int current);

    /// Where player `player`'s pins sit (GUI).
    [[nodiscard]] static GuiPoint pinPlace(std::size_t player);
    /// Where player `player`'s shapes are centred: a point at distance 1 in the overlay camera's space (default mode).
    [[nodiscard]] static graphics::OverlayPoint shapeCentre(std::size_t player);
    /// How many of a wedge's rim vertices on each side of its middle take the colour: floor(32 × `percent` / 100 / 2).
    /// The wedge is that many whole segments each side, then one segment fading to clear; nothing for 0 per cent.
    [[nodiscard]] static int wedgeVertices(float percent);

    /// Adds player `player`'s dial while it is shown: the pins, largest first (`canvas.minigames`), then over every
    /// sprite the two wedges (`canvas.shapes`) and the lock face (`canvas.shapeFace`), in the original's queue order.
    /// @orig 0x001b8ed0 LockPickDial_Render (unknown)
    /// @orig 0x001b8530 LockPickDial_Draw (unknown)
    void render(const HudCanvas& canvas, std::size_t player) const;

  private:
    // One wedge: the whole 32-segment fan, rim vertices within `percent` of 12 o'clock coloured, the rest clear.
    static void renderWedge(graphics::SpriteBatch& batch, graphics::OverlayPoint centre, float percent,
                            graphics::Rgba colour);
    // The lock face: a textured 32-segment disc over the sheet rectangle's inscribed circle, turned a quarter.
    static void renderFace(graphics::SpriteBatch& batch, graphics::OverlayPoint centre);

    bool m_shown = false;
    int m_difficulty = 0;
    std::array<float, kLockPinCount> m_angles{};
    int m_current = 0;
};

} // namespace coney::hud
