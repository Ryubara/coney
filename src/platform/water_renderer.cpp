// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/water_renderer.h"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include "core/name_hash.h"
#include "graphics/frame_description.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"

namespace coney::platform {

WaterRenderer::WaterRenderer(const io::Wad& wad, std::function<void(std::string_view)> print)
    : m_wad(wad), m_print(std::move(print)), m_table(chunk::ChunkHandlerTable::withDefaults()) {
    addTextureDictionaryHandlers(m_table);
}

void WaterRenderer::loadTexture() {
    m_tried = true;
    // The file is named by the decimal CRC-32 of the texture's name (a disc check: `water_tex` is 725908093).
    const std::string fileName = std::to_string(crc32(effects::Water::kTexture));
    const auto report = [this, &fileName](std::string_view why) {
        if (m_print) {
            m_print(std::format("water: texture {} ({}) not loaded: {}\n", effects::Water::kTexture, fileName, why));
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
    if (auto converted = dictionaries->front().convertForDrawing(); !converted) {
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

void WaterRenderer::draw(RenderEngine& engine, const effects::Water& water) {
    if (!water.placed()) {
        return;
    }
    if (!m_tried) {
        loadTexture();
    }
    // The grid in the world, in RenderWare's axes.
    std::vector<graphics::EffectVertex> vertices;
    vertices.reserve(water.vertices().size());
    for (const effects::WaterVertex& source : water.vertices()) {
        const world::Vec3 p = toRenderWare(water.toWorld(source.position));
        vertices.push_back(graphics::EffectVertex{
            p.x, p.y, p.z, graphics::Rgba{source.colour[0], source.colour[1], source.colour[2], source.colour[3]},
            source.u, source.v});
    }

    // Culling off and the texture repeating, blended by the vertices' alpha; Z test, Z write and fog stay as the world
    // pass left them, and the back faces are dropped again after it.
    graphics::EffectDraw draw;
    draw.states.cull = graphics::CullFaces::None;
    draw.states.vertexAlpha = true;
    draw.states.blend = graphics::EffectBlend::Alpha;
    draw.states.filter = graphics::TextureFilter::Linear;
    draw.states.address = graphics::TextureAddress::Wrap;
    draw.restore.cull = true;
    engine.drawEffect(draw, vertices, effects::Water::indices(), m_texture.get());
}

} // namespace coney::platform
