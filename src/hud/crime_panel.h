// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"

namespace coney::hud {

/// The radar frame's arcs (docs/research/hud.md#fn-radar-frame): the wanted pair's radii and the second timer's, in
/// pixels round the radar disc's centre; the default video mode's stretch (x 1.1, y 1.0); the segments; the colours;
/// the least fill drawn; the easing per update and the jump that snaps.
inline constexpr float kCrimeInner = 54.0F;
inline constexpr float kCrimeMiddle = 57.6F;
inline constexpr float kCrimeOuter = 61.2F;
inline constexpr float kCrimeStretchX = 1.1F;
inline constexpr float kCrimeStretchY = 1.0F;
inline constexpr int kCrimeSegments = 16;
inline constexpr graphics::Rgba kWantedColour{0x23, 0x53, 0xbc, 0xff};
inline constexpr graphics::Rgba kSecondTimerColour{0xc9, 0x6b, 0x2e, 0xff};
inline constexpr float kCrimeArcMinimum = 0.03F;
inline constexpr float kCrimeEase = 0.05F;
inline constexpr float kCrimeSnap = 0.5F;
/// A timer's fill: the fraction of 10 s left, full above this.
inline constexpr float kCrimeFullAbove = 0.9F;

/// The radar frame (`HudCrimePanel`, HUD `+0x177d0` for player 0): four flat-colour arcs round the radar disc. The
/// inner pair shows the time the player's gang stays wanted, growing up both sides from the bottom and meeting at the
/// top when full; the outer pair the gang's second timer, in orange (at the inner radii when the wanted pair is not
/// drawn). Each shown fill eases 5 % an update toward its timer's.
///
/// **Coney's placement**: the easing runs in update() (the HUD's fixed step), where the original eases in each draw,
/// which it makes once per update.
///
/// Research: docs/research/hud.md#fn-radar-frame
class CrimePanel {
  public:
    /// The fill of a timer with `fraction` of its 10 s left: 1 above 0.9, else the fraction, 0 for none.
    [[nodiscard]] static float fillOf(float fraction);

    /// One HUD update with the wanted and second timers' fractions left: each pair's target, then its shown fill eased
    /// toward it (snapping on a jump over 0.5).
    /// @orig 0x001aa9b0 HudCrimePanel_Update (unknown)
    void update(float wanted, float second);
    /// The shown fills of the wanted and the second pair.
    [[nodiscard]] float wantedShown() const { return m_shown[0]; }
    [[nodiscard]] float secondShown() const { return m_shown[1]; }

    /// Adds the arcs round `centre` (the radar disc's, in overlay space at depth 1) into `batch` as triangles: the
    /// wanted pair while its target or fill is above 0.03, then the second pair outside it (or in its place).
    /// @orig 0x001aaba8 HudCrimePanel_Render (unknown)
    void render(graphics::SpriteBatch& batch, graphics::OverlayPoint centre) const;

  private:
    // One arc from 0 to `endDegrees` (±180) at `fill` of its sweep, the band from `inner` to `outer` pixels.
    // @orig 0x001a5e38 RingArc_Draw (unknown)
    static void renderArc(graphics::SpriteBatch& batch, graphics::OverlayPoint centre, float inner, float outer,
                          float endDegrees, float fill, graphics::Rgba colour);

    std::array<float, 2> m_target{};
    std::array<float, 2> m_shown{};
};

} // namespace coney::hud
