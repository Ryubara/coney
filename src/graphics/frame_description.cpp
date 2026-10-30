// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the renderer's seam (docs/engine/renderer.md#frame-description); the original draws straight to its
// device
#include "graphics/frame_description.h"

#include <algorithm>

#include "core/assert.h"
#include "graphics/screen.h"

namespace coney::graphics {

void FrameDescription::begin(const FrameStart& start) {
    m_start = start;
    m_passes.clear();
    m_textures.clear();
    m_quads.clear();
    m_vertices.clear();
}

TextureId FrameDescription::texture(const Texture* texture) {
    if (texture == nullptr) {
        return kNoTexture;
    }
    // A frame uses a handful of textures, so a scan is cheaper than a map.
    const auto found = std::find(m_textures.begin(), m_textures.end(), texture);
    if (found != m_textures.end()) {
        return static_cast<TextureId>(found - m_textures.begin()) + 1;
    }
    m_textures.push_back(texture);
    return static_cast<TextureId>(m_textures.size());
}

const Texture* FrameDescription::textureOf(TextureId id) const {
    if (id == kNoTexture || id > m_textures.size()) {
        return nullptr;
    }
    return m_textures[id - 1];
}

Overlay2dPass& FrameDescription::overlay() {
    if (m_passes.empty() || !std::holds_alternative<Overlay2dPass>(m_passes.back())) {
        m_passes.emplace_back(Overlay2dPass{});
    }
    return std::get<Overlay2dPass>(m_passes.back());
}

void FrameDescription::addQuads(const Material2d& material, std::span<const LogicalQuad> quads,
                                const ScreenRect& onto) {
    if (quads.empty()) {
        return;
    }
    const auto first = static_cast<std::uint32_t>(m_quads.size());
    // Logical pixels to window pixels over `onto`; the texture coordinates and colour as given.
    for (LogicalQuad quad : quads) {
        const LogicalRect rect = logicalToWindow(LogicalRect{quad.x, quad.y, quad.width, quad.height}, onto);
        quad.x = rect.x;
        quad.y = rect.y;
        quad.width = rect.width;
        quad.height = rect.height;
        m_quads.push_back(quad);
    }
    overlay().draws.push_back(Draw2d{Shape2d::Quads, material, first, static_cast<std::uint32_t>(quads.size())});
}

void FrameDescription::addWindowQuads(const Material2d& material, std::span<const LogicalQuad> quads) {
    if (quads.empty()) {
        return;
    }
    const auto first = static_cast<std::uint32_t>(m_quads.size());
    m_quads.insert(m_quads.end(), quads.begin(), quads.end());
    overlay().draws.push_back(Draw2d{Shape2d::Quads, material, first, static_cast<std::uint32_t>(quads.size())});
}

void FrameDescription::addTriangles(const Material2d& material, std::span<const LogicalVertex> vertices,
                                    const ScreenRect& onto) {
    const std::size_t count = vertices.size() / 3 * 3;
    if (count == 0) {
        return;
    }
    const auto first = static_cast<std::uint32_t>(m_vertices.size());
    // Logical pixels to window pixels: a point is a rectangle of no size.
    for (const LogicalVertex& in : vertices.first(count)) {
        const LogicalRect at = logicalToWindow(LogicalRect{in.x, in.y, 0.0F, 0.0F}, onto);
        m_vertices.push_back(ScreenVertex{at.x, at.y, in.u, in.v, in.colour});
    }
    overlay().draws.push_back(Draw2d{Shape2d::Triangles, material, first, static_cast<std::uint32_t>(count)});
}

void FrameDescription::addScreenPass(const ScreenPass& pass) { m_passes.emplace_back(pass); }

std::span<const LogicalQuad> FrameDescription::quadsOf(const Draw2d& draw) const {
    CONEY_ASSERT(draw.shape == Shape2d::Quads && draw.first + draw.count <= m_quads.size());
    return std::span<const LogicalQuad>(m_quads).subspan(draw.first, draw.count);
}

std::span<const ScreenVertex> FrameDescription::verticesOf(const Draw2d& draw) const {
    CONEY_ASSERT(draw.shape == Shape2d::Triangles && draw.first + draw.count <= m_vertices.size());
    return std::span<const ScreenVertex>(m_vertices).subspan(draw.first, draw.count);
}

} // namespace coney::graphics
