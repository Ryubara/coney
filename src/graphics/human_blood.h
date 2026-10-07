// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace coney::graphics {

/// The three shared blood textures a human's second draw pass shows, lightest first. The original keeps them in the
/// resource manager's slots `+0x74`, `+0x78` and `+0x7c`, loaded with `global.pak`.
enum class BloodTexture : std::uint8_t {
    Light,  ///< `charblood_d1`: health 60% and above.
    Medium, ///< `charblood_d2`: health 30% to 60%.
    Heavy,  ///< `charblood_d3`: health below 30%.
};

/// The texture names, as the dictionaries in `global.pak` name them (confirmed (runtime), docs/research/graphics.md).
[[nodiscard]] constexpr std::string_view bloodTextureName(BloodTexture texture) {
    switch (texture) {
    case BloodTexture::Light:
        return "charblood_d1";
    case BloodTexture::Medium:
        return "charblood_d2";
    case BloodTexture::Heavy:
        return "charblood_d3";
    }
    return "charblood_d1";
}

/// The health percentage below which the blood shows: from 90% up the pass's blend leaves the frame unchanged.
inline constexpr float kBloodShowsBelowPercent = 90.0F;

/// What a human's blood pass (the material's MatFX dual texture, drawn with the model's second texture-coordinate set)
/// does this frame.
struct BloodLayer {
    BloodTexture texture = BloodTexture::Light; ///< The dual texture set on the material.
    bool shows = false; ///< The blend is the ordinary alpha blend (GS `ALPHA` `0x44`); false: `0x45`, a no-op pass.
};

/// The blood layer of a human at `healthPercent` (health / maximum × 100): the texture `min(trunc(h / 30), 2)` picks,
/// heaviest at the lowest health, shown while the health is below 90% and `noBlood` (the game state's `+0x454`, which
/// the retail game never sets) is false.
///
/// Research: docs/research/graphics.md#human-draw, docs/research/rendering.md#characters
/// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
[[nodiscard]] BloodLayer bloodLayerFor(float healthPercent, bool noBlood = false);

} // namespace coney::graphics
