// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// The stereo-theft panel's layout in the default video mode (docs/research/hud.md#stereo-layout): the anchor (player
/// 1's x is mirrored), the backdrop's size, the arrow's corner offsets, the parts' sizes (overlay units, the moving
/// parts square) and the stick's and ring's offsets.
inline constexpr GuiPoint kStereoAnchor{0.12F, 0.57F};
inline constexpr float kStereoSize = 0.08F;
inline constexpr float kStereoCornerY = 0.05F;
inline constexpr float kStereoArrowSize = 0.12F;
inline constexpr float kStereoGaugeMin = 0.025F;
inline constexpr float kStereoGaugeMax = 0.05F;
inline constexpr float kStereoStickSize = 0.055F;
inline constexpr float kStereoRingSize = 0.05F;
inline constexpr float kStereoStickX = -0.015F;
inline constexpr float kStereoRingX = 0.04F;
/// The gauge's first pop step (`+0xb04`), doubling each update while a stage is complete.
inline constexpr float kStereoPopStep = 0.001F;
/// The stick's four pictures each show this long, ms; the ring turns this many radians per ms (backwards).
inline constexpr std::uint64_t kStereoStickFrameMs = 320;
inline constexpr float kStereoRingTurn = -0.004F;
/// The sheets' rectangles: the backdrop, the arrow and the gauge of `hud_minigames`; the ring of `part_page0` (the
/// stick is `part_page0` 3 down to 0).
inline constexpr std::size_t kStereoBackdropRect = 3;
inline constexpr std::size_t kStereoArrowRect = 0;
inline constexpr std::size_t kStereoGaugeRect = 2;
inline constexpr std::size_t kStereoRingRect = 5;
inline constexpr std::size_t kStereoStickFirstRect = 3;
/// The colours: grey 128 for every part but the ring.
inline constexpr graphics::Rgba kStereoGrey{128, 128, 128, 255};
inline constexpr graphics::Rgba kStereoRingColour{225, 186, 65, 255};

/// One player's stereo-theft panel (HUD `+0xfea0` + player × `0xb10`): the car radio's backdrop with an arrow going
/// round its corners, one per stage; a gauge on the arrow that grows and spins with the angle turned and drops off the
/// screen as a stage completes; and a stick turning in a ring beside it, the "rotate the left stick" picture.
///
/// The play mode starts it with the triangle press (the intro clip), sets the progress each update and ends it the
/// moment the success or failure clip starts. update() runs on the fixed step with the game time.
///
/// Research: docs/research/hud.md#stereo-layout
/// @orig 0x001c9818 StereoHud_Construct (unknown)
class StereoHud {
  public:
    /// Shows the panel for a theft whose stages each take `target` radians: stage 0, nothing turned, the pop reset.
    /// @orig 0x001ca118 StereoHud_Start (unknown)
    void start(float target);
    /// This update's progress: `value` radians turned in the current stage (0 to the target) and the stage (0-3).
    /// @orig 0x001ca210 StereoHud_SetProgress (unknown)
    void setProgress(float value, int stage);
    /// Removes the panel.
    /// @orig 0x001ca180 StereoHud_Shutdown (unknown)
    void end() { m_shown = false; }
    /// Whether the panel is up.
    [[nodiscard]] bool shown() const { return m_shown; }

    /// One HUD update at game time `nowMs`: the gauge's pop while the stage is complete (its drop grows by a step that
    /// doubles each update; below the target both reset), and the ring's mirror, which flips every update.
    /// @orig 0x001ca2d0 StereoHud_Update (unknown)
    void update(std::uint64_t nowMs);

    /// The gauge's size now, 0.025 at nothing turned to 0.05 at the target.
    [[nodiscard]] float gaugeSize() const;
    /// The arrow's place for the stage (the backdrop's corner) and its turn, radians.
    [[nodiscard]] GuiPoint arrowPlace(std::size_t player) const;
    [[nodiscard]] float arrowAngle() const;
    /// How far the gauge has dropped below the arrow (the pop).
    [[nodiscard]] float drop() const { return m_drop; }
    /// The stick picture's `part_page0` rectangle now (3, 2, 1, 0 every 320 ms).
    [[nodiscard]] std::size_t stickRect() const;
    /// The stick's place for the stage: below the backdrop in stages 0 and 1, above it in 2 and 3.
    [[nodiscard]] GuiPoint stickPlace(std::size_t player) const;

    /// Adds player `player`'s panel: the backdrop, then the arrow, the gauge, the stick and the ring.
    /// @orig 0x001ca260 StereoHud_Render (unknown)
    void render(const HudCanvas& canvas, std::size_t player) const;

  private:
    bool m_shown = false;
    float m_target = 1.0F;         // +0x4c in the panel: the stage's angle
    float m_value = 0.0F;          // +0x48
    int m_stage = 0;               // +0x44
    float m_step = kStereoPopStep; // +0xb04
    float m_drop = 0.0F;           // the gauge's y offset
    bool m_mirrored = false;       // the ring's u0 and u1 swapped
    std::uint64_t m_nowMs = 0;
};

} // namespace coney::hud
