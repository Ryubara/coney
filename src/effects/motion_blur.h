// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::effects {

/// The motion-blur part of a view's screen-effects manager (docs/research/graphics.md#screen-effects): a colour and
/// a strength (its alpha, 0-255) that `QueueMotionBlurEffect` blends to over a time, and the platform draws each frame
/// as the last frame laid over the new one (src/platform/motion_blur_pass.h).
///
/// **Coney's stand-ins** where the page is silent: the blend is linear in time; the colour starts white with strength
/// 0 (the looks that also set a strength, `CfgScrFx`, are not applied yet); a blend of 0 seconds or less is at once.
///
/// Research: docs/research/graphics.md#looks, docs/references/bindings/effects.md#queuemotionblureffect
class MotionBlur {
  public:
    /// A colour of the blur, `{r, g, b, a}` with `a` the strength.
    struct Colour {
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 0;

        friend bool operator==(const Colour&, const Colour&) = default;
    };

    /// `QueueMotionBlurEffect(alpha, seconds)`: blends the strength to `alpha` over `seconds`, the colour kept.
    /// @orig 0x0040cce8 ScreenFx_QueueMotionBlurAlpha (unknown)
    void queueAlpha(std::uint8_t alpha, float seconds);
    /// `QueueMotionBlurEffect({r, g, b, a}, seconds)`: blends colour and strength to `target` over `seconds`.
    /// @orig 0x0040cd28 ScreenFx_QueueMotionBlurColour (unknown)
    void queueColour(Colour target, float seconds);

    /// Advances the blend by `seconds`.
    /// @orig 0x0018c8c8 ScreenFx_BlendMotionBlur (unknown)
    void step(float seconds);

    /// The colour and strength now.
    [[nodiscard]] Colour current() const;
    /// Where the blend is going.
    [[nodiscard]] Colour target() const { return m_to; }
    /// Whether a blend is under way.
    [[nodiscard]] bool blending() const { return m_elapsed < m_seconds; }
    /// Back to no blur at once (a level is unloaded).
    void reset() { *this = MotionBlur{}; }

  private:
    Colour m_from;
    Colour m_to;
    float m_seconds = 0.0F; // the blend's length
    float m_elapsed = 0.0F; // how far into it
};

} // namespace coney::effects
