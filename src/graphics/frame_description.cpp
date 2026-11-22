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
    m_lights.clear();
    m_sceneTextures.clear();
    m_bones.clear();
    m_viewColours.clear();
    m_effectVertices.clear();
    m_effectIndices.clear();
    m_camera.reset();
}

FrameTextureId FrameDescription::texture(const Texture* texture) {
    if (texture == nullptr) {
        return kNoFrameTexture;
    }
    // A frame uses a handful of textures, so a scan is cheaper than a map.
    const auto found = std::find(m_textures.begin(), m_textures.end(), texture);
    if (found != m_textures.end()) {
        return static_cast<FrameTextureId>(found - m_textures.begin()) + 1;
    }
    m_textures.push_back(texture);
    return static_cast<FrameTextureId>(m_textures.size());
}

const Texture* FrameDescription::textureOf(FrameTextureId id) const {
    if (id == kNoFrameTexture || id > m_textures.size()) {
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

ScenePass& FrameDescription::scene() {
    // A new scene pass keeps the camera set last this frame.
    if (m_passes.empty() || !std::holds_alternative<ScenePass>(m_passes.back())) {
        m_passes.emplace_back(ScenePass{m_camera, false, {}, {}});
    }
    return std::get<ScenePass>(m_passes.back());
}

EffectPass& FrameDescription::effects() {
    // A new effect pass keeps the camera set last this frame.
    if (m_passes.empty() || !std::holds_alternative<EffectPass>(m_passes.back())) {
        m_passes.emplace_back(EffectPass{m_camera, {}});
    }
    return std::get<EffectPass>(m_passes.back());
}

void FrameDescription::addScreenPass(const ScreenPass& pass) { m_passes.emplace_back(pass); }

void FrameDescription::setSceneCamera(const SceneCamera& camera) {
    m_camera = camera;
    // A scene pass with no draw yet takes it; else the draws after it start a new pass.
    if (!m_passes.empty()) {
        if (auto* last = std::get_if<ScenePass>(&m_passes.back());
            last != nullptr && last->draws.empty() && last->lines.empty()) {
            last->camera = camera;
            return;
        }
    }
    m_passes.emplace_back(ScenePass{camera, false, {}, {}});
}

void FrameDescription::clearSceneDepth() {
    // A scene pass with no draw yet clears instead; else the draws after it start a new pass that clears first.
    if (!m_passes.empty()) {
        if (auto* last = std::get_if<ScenePass>(&m_passes.back());
            last != nullptr && last->draws.empty() && last->lines.empty()) {
            last->clearDepth = true;
            return;
        }
    }
    m_passes.emplace_back(ScenePass{m_camera, true, {}, {}});
}

void FrameDescription::addSceneDraw(Draw3d draw, std::span<const SceneLight> lights,
                                    std::span<const TextureId> textures, std::span<const TextureId> dualTextures,
                                    std::span<const Transform3d> bones, std::span<const Rgba> viewColours) {
    // The ranges point into the frame's arrays, after what earlier draws added.
    draw.firstLight = static_cast<std::uint32_t>(m_lights.size());
    draw.lightCount = static_cast<std::uint32_t>(lights.size());
    m_lights.insert(m_lights.end(), lights.begin(), lights.end());
    draw.firstTexture = static_cast<std::uint32_t>(m_sceneTextures.size());
    draw.textureCount = static_cast<std::uint32_t>(textures.size());
    m_sceneTextures.insert(m_sceneTextures.end(), textures.begin(), textures.end());
    draw.firstDualTexture = static_cast<std::uint32_t>(m_sceneTextures.size());
    draw.dualTextureCount = static_cast<std::uint32_t>(dualTextures.size());
    m_sceneTextures.insert(m_sceneTextures.end(), dualTextures.begin(), dualTextures.end());
    draw.firstBone = static_cast<std::uint32_t>(m_bones.size());
    draw.boneCount = static_cast<std::uint32_t>(bones.size());
    m_bones.insert(m_bones.end(), bones.begin(), bones.end());
    draw.firstViewColour = static_cast<std::uint32_t>(m_viewColours.size());
    draw.viewColourCount = static_cast<std::uint32_t>(viewColours.size());
    m_viewColours.insert(m_viewColours.end(), viewColours.begin(), viewColours.end());
    scene().draws.push_back(draw);
}

std::span<const LogicalQuad> FrameDescription::quadsOf(const Draw2d& draw) const {
    CONEY_ASSERT(draw.shape == Shape2d::Quads && draw.first + draw.count <= m_quads.size());
    return std::span<const LogicalQuad>(m_quads).subspan(draw.first, draw.count);
}

std::span<const ScreenVertex> FrameDescription::verticesOf(const Draw2d& draw) const {
    CONEY_ASSERT(draw.shape == Shape2d::Triangles && draw.first + draw.count <= m_vertices.size());
    return std::span<const ScreenVertex>(m_vertices).subspan(draw.first, draw.count);
}

void FrameDescription::addEffectDraw(EffectDraw draw, std::span<const EffectVertex> vertices,
                                     std::span<const std::uint16_t> indices, const Texture* texture) {
    if (vertices.empty()) {
        return;
    }
    // The ranges point into the frame's arrays, after what earlier draws added.
    draw.texture = this->texture(texture);
    draw.firstVertex = static_cast<std::uint32_t>(m_effectVertices.size());
    draw.vertexCount = static_cast<std::uint32_t>(vertices.size());
    m_effectVertices.insert(m_effectVertices.end(), vertices.begin(), vertices.end());
    draw.firstIndex = static_cast<std::uint32_t>(m_effectIndices.size());
    draw.indexCount = static_cast<std::uint32_t>(indices.size());
    m_effectIndices.insert(m_effectIndices.end(), indices.begin(), indices.end());
    effects().draws.push_back(draw);
}

std::span<const EffectVertex> FrameDescription::effectVerticesOf(const EffectDraw& draw) const {
    CONEY_ASSERT(draw.firstVertex + draw.vertexCount <= m_effectVertices.size());
    return std::span<const EffectVertex>(m_effectVertices).subspan(draw.firstVertex, draw.vertexCount);
}

std::span<const std::uint16_t> FrameDescription::effectIndicesOf(const EffectDraw& draw) const {
    CONEY_ASSERT(draw.firstIndex + draw.indexCount <= m_effectIndices.size());
    return std::span<const std::uint16_t>(m_effectIndices).subspan(draw.firstIndex, draw.indexCount);
}

std::span<const SceneLight> FrameDescription::lightsOf(const Draw3d& draw) const {
    CONEY_ASSERT(draw.firstLight + draw.lightCount <= m_lights.size());
    return std::span<const SceneLight>(m_lights).subspan(draw.firstLight, draw.lightCount);
}

std::span<const TextureId> FrameDescription::texturesOf(const Draw3d& draw) const {
    CONEY_ASSERT(draw.firstTexture + draw.textureCount <= m_sceneTextures.size());
    return std::span<const TextureId>(m_sceneTextures).subspan(draw.firstTexture, draw.textureCount);
}

std::span<const TextureId> FrameDescription::dualTexturesOf(const Draw3d& draw) const {
    CONEY_ASSERT(draw.firstDualTexture + draw.dualTextureCount <= m_sceneTextures.size());
    return std::span<const TextureId>(m_sceneTextures).subspan(draw.firstDualTexture, draw.dualTextureCount);
}

std::span<const Transform3d> FrameDescription::bonesOf(const Draw3d& draw) const {
    CONEY_ASSERT(draw.firstBone + draw.boneCount <= m_bones.size());
    return std::span<const Transform3d>(m_bones).subspan(draw.firstBone, draw.boneCount);
}

void FrameDescription::addSceneLines(std::span<const SceneLineVertex> lines) {
    // A vertex with no partner is no line.
    const std::size_t count = lines.size() / 2 * 2;
    if (count == 0) {
        return;
    }
    std::vector<SceneLineVertex>& into = scene().lines;
    into.insert(into.end(), lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(count));
}

std::span<const Rgba> FrameDescription::viewColoursOf(const Draw3d& draw) const {
    CONEY_ASSERT(draw.firstViewColour + draw.viewColourCount <= m_viewColours.size());
    return std::span<const Rgba>(m_viewColours).subspan(draw.firstViewColour, draw.viewColourCount);
}

} // namespace coney::graphics
