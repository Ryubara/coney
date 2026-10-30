// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the renderer's seam (docs/engine/renderer.md#frame-description); the original draws straight to its
// device
#include "graphics/frame_description.h"

#include <algorithm>

#include "core/assert.h"
#include "graphics/screen.h"

namespace coney::graphics {

// Stubs until the description is built: every call keeps nothing.
void FrameDescription::begin(const FrameStart& /*start*/) {}

FrameTextureId FrameDescription::texture(const Texture* /*texture*/) { return kNoFrameTexture; }

const Texture* FrameDescription::textureOf(FrameTextureId /*id*/) const { return nullptr; }

Overlay2dPass& FrameDescription::overlay() {
    if (m_passes.empty() || !std::holds_alternative<Overlay2dPass>(m_passes.back())) {
        m_passes.emplace_back(Overlay2dPass{});
    }
    return std::get<Overlay2dPass>(m_passes.back());
}

void FrameDescription::addQuads(const Material2d& /*material*/, std::span<const LogicalQuad> /*quads*/,
                                const ScreenRect& /*onto*/) {}

void FrameDescription::addWindowQuads(const Material2d& /*material*/, std::span<const LogicalQuad> /*quads*/) {}

void FrameDescription::addTriangles(const Material2d& /*material*/, std::span<const LogicalVertex> /*vertices*/,
                                    const ScreenRect& /*onto*/) {}

void FrameDescription::addScreenPass(const ScreenPass& /*pass*/) {}

std::span<const LogicalQuad> FrameDescription::quadsOf(const Draw2d& /*draw*/) const { return {}; }

std::span<const ScreenVertex> FrameDescription::verticesOf(const Draw2d& /*draw*/) const { return {}; }

} // namespace coney::graphics
