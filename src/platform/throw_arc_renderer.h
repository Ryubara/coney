// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "core/chunk_system.h"
#include "fileio/wad.h"
#include "graphics/throw_arc.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

/// Draws player 1's throw-aim arc (graphics::throwArcStrip()) while he aims: the ribbon with back-face culling off, as
/// `ThrowArc_Render` draws its clump, textured with `aimthrow` (the WAD file named by the decimal CRC-32 of
/// `aimthrow_tex`), wrapping, linearly filtered. The texture is loaded the first time the arc is drawn; one that does
/// not load is reported once through `print` and the arc is drawn untextured in its colour. **Coney's stand-in** for
/// what the page leaves to the world pass's defaults (inferred): Z tested, not written, blended by the vertices' alpha
/// with the texture's, no fog. The vertex colour and alpha are used as RenderWare's GL pipeline reads them, 255 as 1.0.
///
/// Research: docs/research/graphics.md#throw-arc
/// @orig 0x00190588 ThrowArc_Render (unknown)
class ThrowArcRenderer {
  public:
    /// Loads the texture from `wad` (which must outlive this) when first needed. Must be destroyed before the
    /// RenderEngine stops.
    ThrowArcRenderer(const io::Wad& wad, std::function<void(std::string_view)> print);

    /// Draws the ribbon `strip` (graphics::throwArcStrip(), game axes) through the current camera.
    void draw(const std::vector<graphics::ThrowArcVertex>& strip);

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
