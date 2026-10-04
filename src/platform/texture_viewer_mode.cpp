// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/texture_viewer_mode.h"

#include <cstddef>
#include <utility>

#include <rw.h>

namespace coney::platform {

TextureViewerMode::TextureViewerMode(RenderEngine& engine, std::vector<TextureDictionary> dictionaries)
    : m_engine(engine), m_dictionaries(std::move(dictionaries)) {
    for (const TextureDictionary& dictionary : m_dictionaries) {
        for (rw::Texture* texture : dictionary.textures()) {
            m_textures.push_back(texture);
            const rw::Raster* raster = texture->raster;
            m_sizes.push_back(raster != nullptr ? graphics::Extent{raster->width, raster->height} : graphics::Extent{});
        }
    }
}

// One frame: clear, lay the textures out for the current window size and draw each in its cell, present.
ModeResult TextureViewerMode::update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) {
    m_engine.beginFrame(kClearColour);
    if (m_engine.drawsPixels()) {
        const graphics::GridLayout layout = graphics::layoutGrid(m_sizes, m_engine.frameSize(), kMargin);
        for (std::size_t i = 0; i < m_textures.size(); ++i) {
            if (m_textures[i]->raster != nullptr && layout.quads[i].width > 0 && layout.quads[i].height > 0) {
                m_engine.drawTexture(m_textures[i], layout.quads[i]);
            }
        }
    }
    m_engine.present();
    return ModeResult::Stay;
}

} // namespace coney::platform
