// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "graphics/light_manager.h"
#include "graphics/render_device.h"

namespace coney::graphics {

/// The world's fog, which is also the background colour: the device's `+0x440` and its fog start `+0x444`.
///
/// Research: docs/research/world.md#fog, docs/research/lighting.md#world
struct WorldFog {
    /// White, as the device starts; a level sets it with `SetFogColor`.
    Rgba colour{255, 255, 255, 255};
    /// The fraction of the far clip where the fog begins (`SetFogDistance`).
    float start = 0.5F;
};

/// `SetFogColor(r, g, b)`'s colour: each component × 255, truncated (a negative one is 0) to its low byte, alpha 255.
/// @orig 0x0040c868 Level_SetFogColour (unknown)
[[nodiscard]] Rgba fogColourOf(float r, float g, float b);

/// The level's shadow light: the one light every shadow map is drawn from, as `global.lua` sets it once per level
/// from its light table (`xshadowcolor`, `xshadowdir`). Render-side state: the simulation never reads it.
///
/// On the PS2 the two bindings that set it are empty; the Xbox port keeps the values in its shadow object
/// (`GraphicsShadowXBox`: colour `+0x14c`, offset `+0x15c`, defaults set at Xbox `0x000bda20`), and Coney keeps them
/// as the Xbox does so the shadow-map presets can read them.
///
/// Research: docs/research/xbox-lighting.md#shadow-light
struct ShadowLight {
    /// The Xbox shadow object's defaults (Xbox `0x000bda20`): a 0.7 grey colour and the offset (-500, -500, 850).
    static constexpr LightColour kDefaultColour{0.7F, 0.7F, 0.7F, 1.0F};
    static constexpr world::Vec3 kDefaultOffset{-500.0F, -500.0F, 850.0F};

    /// The shadow colour: what a receiver's lit colour is multiplied by where the shadow falls.
    LightColour colour = kDefaultColour;
    /// Where the light stands from the casters' centre, in the game's axes (z up), metres.
    world::Vec3 offset = kDefaultOffset;
    /// Whether a script set the colour or the offset. **Coney's choice** (a stand-in): a level whose scripts never set
    /// its shadow light has none, so a Coney level without one keeps the PS2's blob in every preset. The Xbox always
    /// has its default shadow light (Xbox `0x000bda20`) and would use the defaults; its levels all set both values.
    bool scripted = false;

    /// `SetShadowColor(r, g, b)`: the shadow colour, alpha 1.
    /// @orig 0x0017f260 Shadow_SetColour_Stub (unknown)
    /// @orig xbox 0x0009ce50 Shadow_SetColour_Stub (unknown)
    void setColour(float r, float g, float b);
    /// `SetShadowLightOffset(x, y, z)`: the offset, in the game's axes.
    /// @orig 0x0017f268 Shadow_SetLightOffset_Stub (unknown)
    /// @orig xbox 0x0009ced0 Shadow_SetLightOffset_Stub (unknown)
    void setOffset(float x, float y, float z);
};

/// Who casts a shadow: every object does unless a script turned its shadow off with `EnableShadow(object, false)`.
/// Kept by script handle (an id, never a pointer), render-side: the stealth "hidden in shadow" rule never reads it.
///
/// The Xbox keeps the flag on the human (`+0x18b`, set to 1 when a human is set up, Xbox `0x0014bb80`); Coney keeps
/// the handles whose flag is off, so a human made after the call keeps his own default.
///
/// Research: docs/research/xbox-lighting.md#shadow-map
class ShadowCasters {
  public:
    /// `EnableShadow(object, enable)`: whether `object` casts a shadow.
    /// @orig 0x0017f270 Shadow_Enable_Stub (unknown)
    /// @orig xbox 0x0009cdd0 Shadow_Enable_Stub (unknown)
    void setEnabled(std::uint32_t object, bool enable);
    /// Whether `object` casts a shadow: true unless a script turned it off.
    /// @coney-only the Xbox reads the human's flag (`+0x18b`) where it draws; Coney looks the handle up
    [[nodiscard]] bool enabled(std::uint32_t object) const;
    /// The objects whose shadow a script turned off, in increasing order.
    [[nodiscard]] std::span<const std::uint32_t> disabled() const { return m_disabled; }

  private:
    std::vector<std::uint32_t> m_disabled; // sorted, no repeats
};

/// What a level's scripts set up for its look and the renderer draws with: the light manager, the fog, the shadow
/// light and who casts a shadow. One per level; the lighting bindings (scripting/lighting_bindings.h) write it.
struct LevelLighting {
    LightManager lights;
    WorldFog fog;
    ShadowLight shadow;
    ShadowCasters casters;
};

} // namespace coney::graphics
