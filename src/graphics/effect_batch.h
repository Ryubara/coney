// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the GPU renderer's reading of the frame description's effect passes
// (docs/engine/gpu-renderer.md#effects); the original draws its sprites straight to the GS
#pragma once

#include "graphics/frame_description.h"
#include "graphics/mesh_data.h"
#include "graphics/overlay_batch.h"

namespace coney::graphics {

/// What an effect draw is drawn in, worked out from the states it sets and those in force before it, as librw's GL3
/// device draws an Im3D primitive (the GL3 renderer's drawEffect()): each depth, culling and fog state the draw's own
/// or else the one in force; blended only when the vertices' alpha is used or the texture has a texel that is not
/// opaque (gl3device.cpp's setVertexAlpha and setRasterStage turn blending and the alpha test on together), then by the
/// blend the draw sets or the one in force, keeping the fragments the alpha test in force keeps against librw's
/// reference after its start (kStartAlphaRef); otherwise replacing what is below with every fragment kept.
struct EffectLook {
    bool depthTest = true;
    bool depthWrite = true;
    CullFaces cull = CullFaces::None;
    bool fog = false;
    Rgba fogColour = kWhite;
    BlendInForce blend = BlendInForce::Replace;
    AlphaRange alpha;

    friend bool operator==(const EffectLook&, const EffectLook&) = default;
};

/// How `draw` is drawn when its texture has alpha (`textureAlpha`: graphics::hasAlpha() of its pixels; false for no
/// texture, and for a line list, which librw draws untextured): EffectLook.
[[nodiscard]] EffectLook effectLookOf(const EffectDraw& draw, bool textureAlpha);

/// An effect's corner as a vertex of the scene's shaders: its place, its colour as the prelighting (the shaders take it
/// as it is when nothing lights it) and its texture coordinate; no normal, no pose.
[[nodiscard]] MeshVertex meshVertexOf(const EffectVertex& vertex);

} // namespace coney::graphics
