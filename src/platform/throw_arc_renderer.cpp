// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/throw_arc_renderer.h"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include <rw.h>

#include "core/name_hash.h"
#include "graphics/throw_arc.h"

namespace coney::platform {

ThrowArcRenderer::ThrowArcRenderer(const io::Wad& wad, std::function<void(std::string_view)> print)
    : m_wad(wad), m_print(std::move(print)), m_table(chunk::ChunkHandlerTable::withDefaults()) {
    addTextureDictionaryHandlers(m_table);
}

void ThrowArcRenderer::loadTexture() {
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
    if (auto converted = dictionaries->front().convertForDrawing(); !converted) {
        report(converted.error().message);
        return;
    }
    m_dictionary = std::move(dictionaries->front());
}

void ThrowArcRenderer::draw(std::span<const anim::Vec3> points, bool hit) {
    const std::vector<graphics::ThrowArcVertex> strip = graphics::throwArcStrip(points, hit);
    if (strip.empty()) {
        return;
    }
    if (!m_tried) {
        loadTexture();
    }
    // The strip's pairs as triangles, in RenderWare's axes: the game's (x, y, z) is RenderWare's (x, z, -y).
    std::vector<rw::gl3::Im3DVertex> vertices;
    vertices.reserve(strip.size());
    for (const graphics::ThrowArcVertex& source : strip) {
        rw::gl3::Im3DVertex vertex{};
        vertex.setX(source.position.x);
        vertex.setY(source.position.z);
        vertex.setZ(-source.position.y);
        vertex.setColor(source.rgba[0], source.rgba[1], source.rgba[2], source.rgba[3]);
        vertex.setU(source.u);
        vertex.setV(source.v);
        vertices.push_back(vertex);
    }
    std::vector<rw::uint16> indices;
    indices.reserve((strip.size() / 2 - 1) * 6);
    for (std::size_t pair = 0; pair + 1 < strip.size() / 2; ++pair) {
        const auto a = static_cast<rw::uint16>(pair * 2);
        indices.insert(indices.end(), {a, static_cast<rw::uint16>(a + 1), static_cast<rw::uint16>(a + 2),
                                       static_cast<rw::uint16>(a + 1), static_cast<rw::uint16>(a + 3),
                                       static_cast<rw::uint16>(a + 2)});
    }
    rw::Raster* raster = m_dictionary ? m_dictionary->textures().front()->raster : nullptr;
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 0);
    rw::SetRenderState(rw::FOGENABLE, 0);
    rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
    rw::SetRenderState(rw::VERTEXALPHA, 1);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    rw::SetRenderStatePtr(rw::TEXTURERASTER, raster);
    rw::im3d::Transform(vertices.data(), static_cast<rw::int32>(vertices.size()), nullptr,
                        rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA | rw::im3d::VERTEXUV);
    rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, indices.data(), static_cast<rw::int32>(indices.size()));
    rw::im3d::End();
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
}

} // namespace coney::platform
