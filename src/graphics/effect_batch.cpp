// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/effect_batch.h"

namespace coney::graphics {

EffectLook effectLookOf(const EffectDraw& draw, bool textureAlpha) {
    const EffectStates& sets = draw.states;
    const EffectInForce& inForce = draw.inForce;
    // Each state the draw sets, else the one in force (the GL3 renderer's drawEffect() sets only those it names).
    EffectLook look;
    look.depthTest = sets.depthTest.value_or(inForce.depthTest);
    look.depthWrite = sets.depthWrite.value_or(inForce.depthWrite);
    look.cull = sets.cull.value_or(inForce.cull);
    look.fog = sets.fog.value_or(inForce.fog);
    look.fogColour = inForce.fogColour;
    // librw turns blending and the alpha test on together, for the vertex alpha or a texture with alpha, and off
    // otherwise: then the draw replaces what is below and keeps every fragment.
    const bool vertexAlpha = sets.vertexAlpha.value_or(inForce.vertexAlpha);
    if (!vertexAlpha && !textureAlpha) {
        look.blend = BlendInForce::Replace;
        return look;
    }
    if (sets.blend) {
        look.blend = *sets.blend == EffectBlend::Additive ? BlendInForce::Additive : BlendInForce::Alpha;
    } else {
        look.blend = inForce.blend;
    }
    // The alpha test in force, against librw's reference after its start.
    switch (inForce.alphaTest) {
    case AlphaTest::GreaterEqual:
        look.alpha.low = kStartAlphaRef;
        break;
    case AlphaTest::Less:
        look.alpha.high = kStartAlphaRef;
        break;
    case AlphaTest::Always:
        break;
    }
    return look;
}

MeshVertex meshVertexOf(const EffectVertex& vertex) {
    MeshVertex out;
    out.position = {vertex.x, vertex.y, vertex.z};
    out.prelit = vertex.colour;
    out.texCoords[0] = {vertex.u, vertex.v};
    return out;
}

} // namespace coney::graphics
