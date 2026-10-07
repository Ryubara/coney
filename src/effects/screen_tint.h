// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// The level's colour wash over the whole screen and the store's: the tint of the screen-effects manager's looks 9 and
// 10. Research: docs/research/rendering.md#tint, docs/research/graphics.md#looks

namespace coney::effects {

/// The screen tint of a view's screen-effects manager (docs/research/rendering.md#tint): `SetLevelColour` sets look
/// 9's colour (the level's wash, from its lighting table) and makes it the base look; `EnterStore` sets look 10's and
/// switches to it, `ExitStore` back to look 9, each blending over 0.25 s. The colour is drawn each frame over the
/// screen, HUD included, as `dst + (rgb − dst) × As / 128` with As = alpha × 128 / 255, truncated; an alpha of 0 is not
/// drawn.
///
/// **Coney's readings** where the pages are silent: `SetLevelColour` changes the tint at once (look 9's in time is 0);
/// the blend is linear in the time and each byte rounded; the other looks (the rage and heat tints, the follower looks)
/// are not modelled, so the current look is always the base look.
class ScreenTint {
  public:
    /// A tint as the manager packs it: each of the script's floats × 255, truncated.
    struct Colour {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 0;

        friend bool operator==(const Colour&, const Colour&) = default;
    };

    /// The level's look, and the store's.
    static constexpr int kLevelLook = 9;
    static constexpr int kStoreLook = 10;
    /// A store's blend, seconds.
    static constexpr float kStoreBlendSeconds = 0.25F;

    /// A script's colour component (0-1) as the manager stores it: × 255, truncated, kept to a byte.
    [[nodiscard]] static std::uint8_t byteOf(double component);
    /// The GS alpha (0-128) an alpha byte is drawn with: × 128 / 255, truncated.
    [[nodiscard]] static int gsAlphaOf(std::uint8_t alpha) { return alpha * 128 / 255; }
    /// How much of the tint's colour covers the screen for an alpha byte: the GS alpha / 128 (0 to 1).
    [[nodiscard]] static float opacityOf(std::uint8_t alpha) { return static_cast<float>(gsAlphaOf(alpha)) / 128.0F; }

    /// `SetLevelColour({r, g, b, a})`: look 9's colour, and look 9 the base look.
    /// @orig 0x0018e5f0 ScreenFx_SetColourOverlay (ScreenEffectsManager.cpp)
    void setLevelColour(Colour colour);
    /// `EnterStore({r, g, b, a})`: look 10's colour, blended to over 0.25 s.
    /// @orig 0x0018e6b8 ScreenFx_EnterStore (ScreenEffectsManager.cpp)
    void enterStore(Colour colour);
    /// `ExitStore()`: back to look 9 over 0.25 s.
    /// @orig 0x0018e780 ScreenFx_ExitStore (ScreenEffectsManager.cpp)
    void exitStore();

    /// Advances the blend by `seconds`.
    void step(float seconds);

    /// The look the tint is going to (kLevelLook or kStoreLook).
    [[nodiscard]] int look() const { return m_look; }
    /// The colour now, along the blend.
    /// @orig 0x0018ca50 ScreenFx_DrawTint (ScreenEffectsManager.cpp)
    [[nodiscard]] Colour current() const;
    /// Whether anything is drawn now (the current alpha is not 0).
    [[nodiscard]] bool drawn() const { return current().a != 0; }
    /// Back to no tint (a level is unloaded).
    void reset() { *this = ScreenTint{}; }

  private:
    // Starts a blend from the colour now to `target` over `seconds` (at once when 0).
    void blendTo(Colour target, float seconds);

    Colour m_level; // look 9's colour
    Colour m_store; // look 10's colour
    int m_look = kLevelLook;
    Colour m_from;
    Colour m_to;
    float m_seconds = 0.0F;
    float m_elapsed = 0.0F;
};

} // namespace coney::effects
