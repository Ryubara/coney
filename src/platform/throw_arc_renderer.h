// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "animation/anim_math.h"
#include "core/chunk_system.h"
#include "fileio/wad.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

/// Draws player 1's throw-aim arc (graphics::throwArcStrip()) while he aims: the strip with back-face culling off, as
/// `ThrowArc_Render` draws its clump. Its texture (`aimthrow_tex`, the WAD file named by the decimal CRC-32 of the
/// name) is loaded the first time the arc is drawn; one that does not load is reported once through `print` and the
/// arc is drawn untextured in its colour. **Coney's stand-in** for the render state the page does not give: Z tested,
/// not written, blended by the vertices' alpha, no fog.
///
/// Research: docs/research/graphics.md#code-textures
/// @orig 0x00190588 ThrowArc_Render (unknown)
class ThrowArcRenderer {
  public:
    /// Loads the texture from `wad` (which must outlive this) when first needed. Must be destroyed before the
    /// RenderEngine stops.
    ThrowArcRenderer(const io::Wad& wad, std::function<void(std::string_view)> print);

    /// Draws the arc along `points` (game axes, the hand first), red when `hit`, through the current camera.
    void draw(std::span<const anim::Vec3> points, bool hit);

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
