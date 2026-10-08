// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/water_renderer.h"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include <rw.h>

#include "core/name_hash.h"
#include "platform/play_scenery.h"

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
    m_dictionary = std::move(dictionaries->front());
}

void WaterRenderer::draw(const effects::Water& water) {
    if (!water.placed()) {
        return;
    }
    if (!m_tried) {
        loadTexture();
    }
    // The grid in the world, in RenderWare's axes.
    std::vector<rw::gl3::Im3DVertex> vertices;
    vertices.reserve(water.vertices().size());
    for (const effects::WaterVertex& source : water.vertices()) {
        const world::Vec3 p = toRenderWare(water.toWorld(source.position));
        rw::gl3::Im3DVertex vertex{};
        vertex.setX(p.x);
        vertex.setY(p.y);
        vertex.setZ(p.z);
        vertex.setColor(source.colour[0], source.colour[1], source.colour[2], source.colour[3]);
        vertex.setU(source.u);
        vertex.setV(source.v);
        vertices.push_back(vertex);
    }
    std::vector<rw::uint16> indices(effects::Water::indices().begin(), effects::Water::indices().end());

    // Culling off and the texture repeating; Z test, Z write and fog stay as the world pass left them.
    rw::Raster* raster = m_dictionary ? m_dictionary->textures().front()->raster : nullptr;
    rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
    rw::SetRenderState(rw::VERTEXALPHA, 1);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    rw::SetRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
    rw::SetRenderState(rw::TEXTUREADDRESS, rw::Texture::WRAP);
    rw::SetRenderStatePtr(rw::TEXTURERASTER, raster);
    rw::im3d::Transform(vertices.data(), static_cast<rw::int32>(vertices.size()), nullptr,
                        rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA | rw::im3d::VERTEXUV);
    rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, indices.data(), static_cast<rw::int32>(indices.size()));
    rw::im3d::End();
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
}

} // namespace coney::platform
