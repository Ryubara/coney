// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's reading of the frame description's effect passes, headless (docs/engine/gpu-renderer.md#effects):
// the states an effect draw is drawn in and its vertices. The GPU renderer has no counterpart in the original; what it
// must match is librw's GL3 device drawing the same Im3D primitive (the GL3 renderer's drawEffect()), the picture it is
// compared with (docs/engine/renderer.md#comparing), so the expected values are librw's, at the pin cmake/deps.cmake
// names: blending and the alpha test turned on together, and only for vertex alpha or a texture with alpha
// (gl3device.cpp's setVertexAlpha and setRasterStage), the alpha test's function used only while it is on
// (setAlphaTest, setAlphaTestFunction) against the reference after its start (resetRenderState: 10 of 255), and a state
// the draw does not set kept as it stands.
#include "graphics/effect_batch.h"

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "graphics/frame_description.h"
#include "graphics/overlay_batch.h"

using coney::graphics::AlphaRange;
using coney::graphics::AlphaTest;
using coney::graphics::BlendInForce;
using coney::graphics::CullFaces;
using coney::graphics::EffectBlend;
using coney::graphics::EffectDraw;
using coney::graphics::EffectLook;
using coney::graphics::EffectVertex;
using coney::graphics::kStartAlphaRef;
using coney::graphics::Rgba;

TEST_CASE("an effect draw keeps the states in force that it does not set", "[graphics][effects]") {
    // Expected: the GL3 renderer's drawEffect() sets only the states the draw names; librw keeps the rest. The water
    // sets no depth or fog state, so it is drawn in the world pass's: depth tested and written, fogged in its colour.
    EffectDraw water;
    water.states.cull = CullFaces::None;
    water.states.vertexAlpha = true;
    water.states.blend = EffectBlend::Alpha;
    water.inForce = {.depthTest = true,
                     .depthWrite = true,
                     .cull = CullFaces::Back,
                     .fog = true,
                     .fogColour = Rgba{10, 20, 30, 255},
                     .vertexAlpha = false,
                     .blend = BlendInForce::Alpha,
                     .alphaTest = AlphaTest::GreaterEqual};
    const EffectLook look = coney::graphics::effectLookOf(water, false);
    CHECK(look.depthTest);
    CHECK(look.depthWrite);
    CHECK(look.cull == CullFaces::None);
    CHECK(look.fog);
    CHECK(look.fogColour == Rgba{10, 20, 30, 255});
    CHECK(look.blend == BlendInForce::Alpha);
    CHECK(look.alpha == AlphaRange{kStartAlphaRef, 1000.0F});

    // A sprite that sets them draws in its own.
    EffectDraw sprite = water;
    sprite.states.depthWrite = false;
    sprite.states.fog = false;
    sprite.states.blend = EffectBlend::Additive;
    const EffectLook own = coney::graphics::effectLookOf(sprite, false);
    CHECK(own.depthTest);
    CHECK_FALSE(own.depthWrite);
    CHECK_FALSE(own.fog);
    CHECK(own.blend == BlendInForce::Additive);
}

TEST_CASE("an effect draw blends only for vertex alpha or a texture with alpha", "[graphics][effects]") {
    // Expected: librw turns blending and the alpha test on together when the vertex alpha is used or the raster has
    // alpha, and off otherwise (setVertexAlpha, setRasterStage); off, a draw replaces what is below and keeps every
    // fragment, whatever blend and alpha test are in force.
    EffectDraw draw;
    draw.inForce.blend = BlendInForce::Additive;
    draw.inForce.alphaTest = AlphaTest::GreaterEqual;
    draw.states.vertexAlpha = false;
    const EffectLook opaque = coney::graphics::effectLookOf(draw, false);
    CHECK(opaque.blend == BlendInForce::Replace);
    CHECK(opaque.alpha == AlphaRange{});

    // The texture's alpha alone turns them on, in the blend in force.
    const EffectLook textured = coney::graphics::effectLookOf(draw, true);
    CHECK(textured.blend == BlendInForce::Additive);
    CHECK(textured.alpha == AlphaRange{kStartAlphaRef, 1000.0F});

    // The vertex alpha in force does too, when the draw leaves it.
    draw.states.vertexAlpha.reset();
    draw.inForce.vertexAlpha = true;
    CHECK(coney::graphics::effectLookOf(draw, false).blend == BlendInForce::Additive);
}

TEST_CASE("an effect draw keeps the fragments the alpha test in force keeps", "[graphics][effects]") {
    // Expected: librw's DoAlphaTest against the reference 10 of 255: "at least" keeps from it up, "less" below it,
    // "always" every one (the bounds librw uses for none, -1000 and 1000).
    EffectDraw draw;
    draw.states.vertexAlpha = true;
    draw.inForce.alphaTest = AlphaTest::Less;
    CHECK(coney::graphics::effectLookOf(draw, false).alpha == AlphaRange{-1000.0F, kStartAlphaRef});
    draw.inForce.alphaTest = AlphaTest::Always;
    CHECK(coney::graphics::effectLookOf(draw, false).alpha == AlphaRange{});
}

TEST_CASE("an effect's corner becomes an unlit vertex of the scene's shaders", "[graphics][effects]") {
    // Expected: librw's im3d.vert passes the corner's colour and texture coordinate through unlit; the scene's vertex
    // shader does the same with a prelit vertex and no light.
    const EffectVertex corner{1.0F, 2.0F, 3.0F, Rgba{40, 50, 60, 70}, 0.25F, 0.75F};
    const coney::graphics::MeshVertex vertex = coney::graphics::meshVertexOf(corner);
    CHECK(vertex.position == std::array<float, 3>{1.0F, 2.0F, 3.0F});
    CHECK(vertex.prelit == Rgba{40, 50, 60, 70});
    CHECK(vertex.texCoords[0] == std::array<float, 2>{0.25F, 0.75F});
    CHECK(vertex.weights == decltype(vertex.weights){});
}
