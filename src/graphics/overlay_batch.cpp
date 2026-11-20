// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/overlay_batch.h"

#include <array>
#include <cmath>
#include <span>
#include <variant>

namespace coney::graphics {

namespace {

// The bound librw's DoAlphaTest uses for "none" on either side.
constexpr float kNoBound = 1000.0F;

// Adds the corners and indices of `draw`, a Quads draw of `frame`, to `batch`.
void addQuads(const FrameDescription& frame, const Draw2d& draw, OverlayBatch& batch) {
    for (const LogicalQuad& quad : frame.quadsOf(draw)) {
        const auto base = static_cast<std::uint32_t>(batch.vertices.size());
        // Clockwise from the top left, as Gl3Renderer::drawWindowQuads makes a quad's corners.
        batch.vertices.push_back(Overlay2dVertex{quad.x, quad.y, quad.uv.u0, quad.uv.v0, quad.colour});
        batch.vertices.push_back(Overlay2dVertex{quad.x + quad.width, quad.y, quad.uv.u1, quad.uv.v0, quad.colour});
        batch.vertices.push_back(
            Overlay2dVertex{quad.x + quad.width, quad.y + quad.height, quad.uv.u1, quad.uv.v1, quad.colour});
        batch.vertices.push_back(Overlay2dVertex{quad.x, quad.y + quad.height, quad.uv.u0, quad.uv.v1, quad.colour});
        for (const std::uint32_t corner : std::array<std::uint32_t, 6>{0, 1, 2, 0, 2, 3}) {
            batch.indices.push_back(base + corner);
        }
    }
}

// Adds the corners and indices of `draw`, a Triangles draw of `frame`, to `batch`.
void addTriangles(const FrameDescription& frame, const Draw2d& draw, OverlayBatch& batch) {
    for (const ScreenVertex& corner : frame.verticesOf(draw)) {
        batch.indices.push_back(static_cast<std::uint32_t>(batch.vertices.size()));
        batch.vertices.push_back(Overlay2dVertex{corner.x, corner.y, corner.u, corner.v, corner.colour});
    }
}

} // namespace

AlphaRange alphaRangeOf(const Material2d& material) {
    if (!material.alphaRef) {
        return AlphaRange{kStartAlphaRef, kNoBound};
    }
    if (*material.alphaRef > 0.0F) {
        // The GL3 renderer hands librw the reference in 8 bits (ALPHATESTREF), which librw divides by 255 again.
        return AlphaRange{static_cast<float>(std::lround(*material.alphaRef * 255.0F)) / 255.0F, kNoBound};
    }
    return AlphaRange{-kNoBound, kNoBound};
}

OverlayBatch batchOverlays(const FrameDescription& frame) {
    OverlayBatch batch;
    for (const FramePass& pass : frame.passes()) {
        batch.passDraws.push_back(static_cast<std::uint32_t>(batch.draws.size()));
        const auto* overlay = std::get_if<Overlay2dPass>(&pass);
        if (overlay == nullptr) {
            ++batch.skippedPasses; // a screen pass, or any pass that is not 2D
            continue;
        }
        for (const Draw2d& draw : overlay->draws) {
            const auto firstIndex = static_cast<std::uint32_t>(batch.indices.size());
            if (draw.shape == Shape2d::Quads) {
                addQuads(frame, draw, batch);
            } else {
                addTriangles(frame, draw, batch);
            }
            const auto indexCount = static_cast<std::uint32_t>(batch.indices.size()) - firstIndex;
            if (indexCount == 0) {
                continue;
            }
            batch.draws.push_back(OverlayDraw{draw.material.texture, draw.material.address, alphaRangeOf(draw.material),
                                              firstIndex, indexCount});
        }
    }
    batch.passDraws.push_back(static_cast<std::uint32_t>(batch.draws.size()));
    return batch;
}

Rgba windowClearOf(const FrameStart& start) { return start.wholeWindow ? start.clear : kBlack; }

} // namespace coney::graphics
