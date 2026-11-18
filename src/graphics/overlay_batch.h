// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the GPU renderer's batching of the frame description (docs/engine/gpu-renderer.md#drawing); the original
// draws straight to its device
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "graphics/frame_description.h"
#include "graphics/render_device.h"
#include "graphics/texture_data.h"

namespace coney::graphics {

/// One corner as the GPU renderer's 2D vertex buffer holds it (the input of `overlay2d.vert.hlsl`): window pixels from
/// the top left, the texture coordinate (0, 0 the texture's top left) and the colour, 20 bytes.
struct Overlay2dVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    Rgba colour;

    friend bool operator==(const Overlay2dVertex&, const Overlay2dVertex&) = default;
};

/// The alpha a 2D draw keeps: a fragment whose alpha (the texel's times the vertex's, 0 to 1) is at least `low` and
/// below `high`, the test librw's GL3 shaders make (`DoAlphaTest` in its `header.frag`), so that both renderers drop
/// the same fragments. The bounds librw uses for "no bound" are kept: -1000 and 1000.
struct AlphaRange {
    float low = -1000.0F;
    float high = 1000.0F;

    friend bool operator==(const AlphaRange&, const AlphaRange&) = default;
};

/// librw's alpha reference after its start, 10 of 255, tested as "at least" (`resetRenderState` in its
/// `gl3device.cpp`). The GL3 renderer draws the 2D quads with the frame's alpha test as it stands (Material2d::alphaRef
/// unset), and every draw that changes the test puts it back, so the quads are drawn with this one: a quad fainter than
/// 10 of 255 draws nothing on GL3, and the GPU renderer does the same (Coney's choice, to match the GL3 picture; what
/// the original's GS test is for the 2D quads is not known).
inline constexpr float kStartAlphaRef = 10.0F / 255.0F;

/// The alpha `material` keeps: unset, GL3's state for the 2D quads (kStartAlphaRef, at least); above 0, at least the
/// reference rounded to 8 bits as the GL3 renderer hands it to librw (ALPHATESTREF); 0, every fragment.
[[nodiscard]] AlphaRange alphaRangeOf(const Material2d& material);

/// One draw of a batch: `indexCount` indices of OverlayBatch::indices from `firstIndex` on, in one texture,
/// addressing and alpha test.
struct OverlayDraw {
    FrameTextureId texture = kNoFrameTexture;
    TextureAddress address = TextureAddress::Clamp;
    AlphaRange alpha;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;

    friend bool operator==(const OverlayDraw&, const OverlayDraw&) = default;
};

/// A frame's 2D draws as one vertex and index buffer and a list of draws, in the frame's order: what the GPU renderer
/// uploads and draws (platform/gpu_renderer.h).
struct OverlayBatch {
    std::vector<Overlay2dVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<OverlayDraw> draws;
    /// The passes left out: the screen passes (the blur pulse, the line blend, the motion blur), which the GPU renderer
    /// draws from item 20 of docs/engine/renderer.md#order, and any pass that is not 2D.
    std::size_t skippedPasses = 0;
};

/// Batches every 2D draw of `frame`'s overlay passes in their order: a quad as four corners clockwise from its top
/// left (as the GL3 renderer's screen quads) and two triangles (0, 1, 2 and 0, 2, 3), a list of triangles as its
/// corners in order; each draw keeps its texture id, addressing and alpha range (alphaRangeOf()). A draw with nothing
/// to draw is left out.
[[nodiscard]] OverlayBatch batchOverlays(const FrameDescription& frame);

/// The colour a frame clears the whole window to before its first pass: the start's colour when it clears the whole
/// window, else black, the logical screen's colour coming with the frame's first draw (RenderEngine::beginFrame()).
[[nodiscard]] Rgba windowClearOf(const FrameStart& start);

} // namespace coney::graphics
