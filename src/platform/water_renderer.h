// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "core/chunk_system.h"
#include "effects/water.h"
#include "fileio/wad.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

/// Draws the level's water surface (effects::Water) after the `d` world, as the world pass does (step 9,
/// docs/research/world.md#a-frame): culling off, Z test, Z write and fog as the world left them, blended by the
/// vertices' alpha, its texture repeating. The texture (`water_tex`, the WAD file named by the decimal CRC-32 of the
/// name) is loaded the first time the water is placed; one that does not load is reported once through `print` and the
/// water is drawn untextured.
///
/// Research: docs/research/graphics.md#code-water
class WaterRenderer {
  public:
    /// Loads the texture from `wad` (which must outlive this) when first needed. Must be destroyed before the
    /// RenderEngine stops.
    WaterRenderer(const io::Wad& wad, std::function<void(std::string_view)> print);

    /// Draws `water` through the current camera when it is placed.
    void draw(const effects::Water& water);

  private:
    // Loads and converts the texture dictionary, once.
    void loadTexture();

    const io::Wad& m_wad;
    std::function<void(std::string_view)> m_print;
    chunk::ChunkHandlerTable m_table;
    std::optional<TextureDictionary> m_dictionary;
    bool m_tried = false; // the texture's load was tried
};

} // namespace coney::platform
