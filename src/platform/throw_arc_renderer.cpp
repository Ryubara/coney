// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/throw_arc_renderer.h"

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/name_hash.h"
#include "graphics/frame_description.h"
#include "graphics/throw_arc.h"
#include "platform/render_engine.h"

namespace coney::platform {

ThrowArcRenderer::ThrowArcRenderer(const io::Wad& wad, std::function<void(std::string_view)> print)
    : m_wad(wad), m_print(std::move(print)), m_table(chunk::ChunkHandlerTable::withDefaults()) {
    addTextureDictionaryHandlers(m_table);
}

void ThrowArcRenderer::loadTexture(bool convert) {
    m_tried = true;
    const std::string fileName = std::to_string(crc32(graphics::kThrowArcTexture));
    const auto report = [this, &fileName](std::string_view why) {
        if (m_print) {
            m_print(std::format("throw: arc texture {} ({}) not loaded, drawn untextured: {}\n",
                                graphics::kThrowArcTexture, fileName, why));
        }
    };
    auto entry = m_wad.lookup(fileName);
    if (!entry) {
        report(entry.error().message);
        return;
    }
    auto dictionaries = loadTextureDictionaries(m_wad, **entry, m_table);
    if (!dictionaries || dictionaries->empty() || dictionaries->front().textures().empty()) {
        report(dictionaries ? "the file holds no texture" : dictionaries.error().message);
        return;
    }
    // Only GL3 draws the raster through librw; the GPU renderer reads it as it is (SheetTexture::toImage()).
    if (auto converted = convert ? dictionaries->front().convertForDrawing() : std::expected<void, Error>{};
        !converted) {
        report(converted.error().message);
        return;
    }
    auto texture = SheetTexture::create(std::move(dictionaries->front()));
    if (!texture) {
        report(texture.error().message);
        return;
    }
    m_texture = std::move(*texture);
}

void ThrowArcRenderer::draw(RenderEngine& engine, const std::vector<graphics::ThrowArcVertex>& strip) {
    if (strip.empty()) {
        return;
    }
    if (!m_tried) {
        loadTexture(engine.drawsPixels());
    }
    // The ribbon's vertices in RenderWare's axes: the game's (x, y, z) is RenderWare's (x, z, -y).
    std::vector<graphics::EffectVertex> vertices;
    vertices.reserve(strip.size());
    for (const graphics::ThrowArcVertex& source : strip) {
        vertices.push_back(graphics::EffectVertex{
            source.position.x, source.position.z, -source.position.y,
            graphics::Rgba{source.rgba[0], source.rgba[1], source.rgba[2], source.rgba[3]}, source.u, source.v});
    }
    // Z tested, not written, no fog, no culling, blended by alpha; the texture repeats once a segment along the ribbon
    // (v runs from 15 to -2), and is filtered linearly. The depth write and the back faces come back after it.
    graphics::EffectDraw draw;
    draw.states.depthTest = true;
    draw.states.depthWrite = false;
    draw.states.fog = false;
    draw.states.cull = graphics::CullFaces::None;
    draw.states.vertexAlpha = true;
    draw.states.blend = graphics::EffectBlend::Alpha;
    draw.states.filter = graphics::TextureFilter::Linear;
    draw.states.address = graphics::TextureAddress::Wrap;
    draw.restore.depthWrite = true;
    draw.restore.cull = true;
    engine.drawEffect(draw, vertices, graphics::throwArcIndices(), m_texture.get());
}

} // namespace coney::platform
