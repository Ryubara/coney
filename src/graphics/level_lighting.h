// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

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

/// `SetFogColor(r, g, b)`'s colour: each component × 255, truncated and clamped, alpha 255.
/// @orig 0x0040c868 Level_SetFogColour (unknown)
[[nodiscard]] Rgba fogColourOf(float r, float g, float b);

/// What a level's scripts set up for its look and the renderer draws with: the light manager and the fog. One per
/// level; the lighting bindings (scripting/lighting_bindings.h) write it.
struct LevelLighting {
    LightManager lights;
    WorldFog fog;
};

} // namespace coney::graphics
