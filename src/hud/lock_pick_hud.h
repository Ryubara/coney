// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "graphics/overlay_camera.h"
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
/// The pins' rest angle (radians): the constant 3.14, not π, where Show starts them and a miss puts them back.
inline constexpr float kLockPinRest = 3.14F;
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

/// The device flags that choose the dial's layout (docs/research/graphics.md#video-mode): interlaced (`0x01`, the
/// default mode alone), 16:9 (`0x04`) and progressive (`0x20`).
inline constexpr std::uint32_t kDeviceInterlaced = 0x01;
inline constexpr std::uint32_t kDeviceWide = 0x04;
inline constexpr std::uint32_t kDeviceProgressive = 0x20;

/// Where one player's dial goes in one video mode and how its parts are scaled: what `LockPickDial_Init` and
/// `LockPickDial_ApplyVideoMode` write for the mode, and what `LockPickDial_Draw` rewrites every update.
struct LockPickLayout {
    /// The pins' centre: a point at distance 1 in the overlay camera's space (the GUI position is never used).
    graphics::OverlayPoint pinCentre;
    /// The wedges' and the lock face's centre, at the same distance; 2.2 pixels from the pins' in the default mode.
    graphics::OverlayPoint shapeCentre;
    /// The wedges' horizontal and vertical radii, in screen pixels.
    graphics::LogicalPoint wedgeRadius;
    /// The lock face's horizontal and vertical radii, in screen pixels.
    graphics::LogicalPoint faceRadius;
    /// The pins' width and height factors (their sizes are kLockPinSize shrinking by kLockPinShrink a pin).
    float pinWidth = 1.0F;
    float pinHeight = 1.0F;
};

/// One player's lock-pick dial (HUD `+0xf420` + player × `0x540`): three pins of shrinking size turning about one
/// centre, each by its own angle, with two red wedges at 12 o'clock (the good and the perfect zone) and the lock face
/// drawn over them. The play mode shows it while the player picks a lock and gives it the pins' angles each update;
/// there is no text.
///
/// Research: docs/research/hud.md#lock-pick-dial-layout
/// @orig 0x001b7eb0 LockPickDial_Init (unknown)
class LockPickHud {
  public:
    /// Shows the dial at `difficulty` (0-2, the wedges' sizes) with every pin at kLockPinRest, or hides it.
    /// @orig 0x001b8428 LockPickDial_Show (unknown)
    void show(bool on, int difficulty = 0);
    [[nodiscard]] bool shown() const { return m_shown; }
    /// This update's pin angles (radians, clockwise on screen) and the pin the next press judges (3 or more when none
    /// is left, which hides the pins).
    void setPins(const std::array<float, kLockPinCount>& angles, int current);
    /// The pins' angles in force (radians).
    [[nodiscard]] const std::array<float, kLockPinCount>& pins() const { return m_angles; }

    /// Lays the dial out for the video mode of device flags `deviceFlags` (kDeviceInterlaced, the default, and the
    /// rest): the mode's centres, radii and pin factors, and the overlay camera it is drawn through.
    /// @orig 0x001b7c10 LockPickDial_ApplyVideoMode (unknown)
    void setVideoMode(std::uint32_t deviceFlags) { m_deviceFlags = deviceFlags; }

    /// Player `player`'s dial in the video mode of device flags `deviceFlags`. Progressive modes have `0x20`, 16:9 ones
    /// `0x04`; the interlaced 4:3 mode alone moves the shapes off the pins' centre, by (-0.005, +0.005). The centres
    /// are what Init and ApplyVideoMode write; the radius and pin factors and the shapes' override what Draw writes
    /// every update.
    ///
    /// Research: docs/research/hud.md#lock-pick-dial-layout
    /// @orig 0x001b7eb0 LockPickDial_Init (unknown)
    /// @orig 0x001b7c10 LockPickDial_ApplyVideoMode (unknown)
    /// @orig 0x001b8530 LockPickDial_Draw (unknown)
    [[nodiscard]] static LockPickLayout layout(std::size_t player, std::uint32_t deviceFlags);
    /// Rim vertex `j` (0 to 32) of a shape with radii `radius` (pixels), as an offset from its centre in screen pixels
    /// with y down: radius × (sin a, cos a), a = π − j × 2π / 32, so vertex 0 is straight up and the rim runs
    /// clockwise.
    ///
    /// Research: docs/research/hud.md#lock-pick-dial-layout
    /// @orig 0x0017bec8 Im2D_DrawArc (unknown)
    /// @orig 0x0017c308 Shape2D_DrawQueued (unknown)
    [[nodiscard]] static graphics::LogicalPoint rimOffset(graphics::LogicalPoint radius, int j);
    /// How many of a wedge's rim vertices on each side of its middle take the colour: floor(32 × `percent` / 100 / 2).
    [[nodiscard]] static int wedgeVertices(float percent);
    /// Whether fan segment `j` (0-31, between rim vertices j and j + 1) belongs to a wedge of `percent`: the original
    /// draws flat-shaded, so each segment takes rim vertex j + 1's colour, and a wedge of k vertices each side is the
    /// 2k + 1 whole segments from -(k + 1) to +k segments off 12 o'clock (half a segment towards 11).
    ///
    /// Research: docs/research/hud.md#lock-pick-dial-layout
    [[nodiscard]] static bool wedgeSegment(float percent, int j);

    /// Adds player `player`'s dial while it is shown: the pins, largest first (`canvas.minigames`), then over every
    /// sprite the two wedges (`canvas.shapes`) and the lock face (`canvas.shapeFace`), in the original's queue order.
    /// @orig 0x001b8ed0 LockPickDial_Render (unknown)
    /// @orig 0x001b8530 LockPickDial_Draw (unknown)
    void render(const HudCanvas& canvas, std::size_t player) const;

  private:
    // One wedge: its fan segments (wedgeSegment), each one solid colour; the clear ones are not drawn.
    static void renderWedge(graphics::SpriteBatch& batch, const graphics::OverlayCamera& camera,
                            graphics::OverlayPoint centre, graphics::LogicalPoint radius, float percent,
                            graphics::Rgba colour);
    // The lock face: a textured 32-segment disc over the sheet rectangle's inscribed circle, upright.
    static void renderFace(graphics::SpriteBatch& batch, const graphics::OverlayCamera& camera,
                           graphics::OverlayPoint centre, graphics::LogicalPoint radius);

    std::uint32_t m_deviceFlags = kDeviceInterlaced;
    bool m_shown = false;
    int m_difficulty = 0;
    std::array<float, kLockPinCount> m_angles{};
    int m_current = 0;
};

} // namespace coney::hud
