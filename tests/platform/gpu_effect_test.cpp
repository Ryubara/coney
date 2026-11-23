// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's effect passes (docs/engine/gpu-renderer.md#effects, item 20 of docs/engine/renderer.md#order):
// Coney-made effect draws (sprites, a surface, a texture with holes, a line) through the frame description's camera
// beside a scene mesh, drawn on a hidden window and read back pixel by pixel. No game data: every vertex and texel is
// made here. The case needs SDL's video and a GPU device, and skips where there is none. The expected colours are
// librw's GL3 device's arithmetic for the same Im3D draws (effect_batch_test.cpp names each source), the picture the
// GPU renderer is compared with.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "support/librw_once.h"
#include <catch2/catch_test_macros.hpp>

#include "graphics/frame_description.h"
#include "platform/draw_calls.h"
#include "platform/pixel_texture.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "support/gpu_capture.h"
#include "support/quad_fixtures.h"

using coney::graphics::EffectBlend;
using coney::graphics::EffectDraw;
using coney::graphics::EffectVertex;
using coney::graphics::Rgba;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WindowDesc;
using coney::test::near;
using coney::test::pixelAt;

namespace {

// The window's size and the camera's view window: at a distance z, a pixel column x lies at (x / 160 - 1) * 0.5 * z and
// a row y at (1 - y / 120) * 0.375 * z, as gpu_scene_test.cpp's cases place their quads.
constexpr int kWidth = 320;
constexpr int kHeight = 240;

// The world point of pixel (`x`, `y`) at distance `z`.
std::array<float, 3> at(float x, float y, float z) {
    return {(x / 160.0F - 1.0F) * 0.5F * z, (1.0F - y / 120.0F) * 0.375F * z, z};
}

// The six corners of a rectangle from (`left`, `top`) to (`right`, `bottom`) pixels at distance `z`, as two triangles
// in `colour`, texture coordinates (0, 0) at its top left to (1, 1) at its bottom right.
std::array<EffectVertex, 6> rectangle(float left, float top, float right, float bottom, float z, Rgba colour) {
    const auto corner = [&](float x, float y, float u, float v) {
        const std::array<float, 3> p = at(x, y, z);
        return EffectVertex{p[0], p[1], p[2], colour, u, v};
    };
    const EffectVertex topLeft = corner(left, top, 0.0F, 0.0F);
    const EffectVertex topRight = corner(right, top, 1.0F, 0.0F);
    const EffectVertex bottomLeft = corner(left, bottom, 0.0F, 1.0F);
    const EffectVertex bottomRight = corner(right, bottom, 1.0F, 1.0F);
    return {topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight};
}

} // namespace

TEST_CASE("the GPU renderer draws an effect pass's sprites blended, added, fogged and depth tested",
          "[render][renderer][window][scene][effects]") {
    // Expected: ../engine/gpu-renderer.md#effects, Coney's own renderer drawing what librw's GL3 device draws for the
    // same Im3D primitives (effect_batch_test.cpp): over the clear colour (100, 100, 100), a red sprite at half vertex
    // alpha is blended to (178, 50, 50); a green one at half alpha added makes (100, 228, 100); a blue sprite behind
    // an opaque grey mesh is hidden by the depth it wrote; a white surface that sets no fog state takes the fog in
    // force, at 75 of a fog from 50 to 100 half its colour (0, 200, 0); a texture whose right texel is clear shows
    // its left (red) and drops the right under the alpha test in force (at least 10 of 255), leaving the clear colour;
    // a yellow line shows along its row.
    if (!coney::test::gpuHere()) {
        SKIP("no display or no GPU device here");
    }
    WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = kWidth;
    desc.height = kHeight;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    REQUIRE(started.has_value());
    RenderEngine& engine = **started;
    coney::platform::SceneAssets& assets = engine.sceneAssets();

    // The opaque grey mesh at 20, over the third column of the top half.
    coney::test::QuadSpec spec;
    spec.topLeft = at(160.0F, 0.0F, 20.0F);
    spec.topRight = at(240.0F, 0.0F, 20.0F);
    spec.bottomLeft = at(160.0F, 120.0F, 20.0F);
    spec.bottomRight = at(240.0F, 120.0F, 20.0F);
    spec.prelit = true;
    spec.prelight = Rgba{128, 128, 128, 255};
    rw::Atomic* quad = coney::test::makeQuad(spec);
    auto mesh = assets.addAtomic(*quad);
    REQUIRE(mesh.has_value());

    // A 2 x 1 texture taken to the nearest texel: red, then clear.
    coney::platform::PixelTexture holes(coney::graphics::TextureFilter::Nearest);
    const std::array<std::uint8_t, 8> texels{255, 0, 0, 255, 0, 0, 0, 0};
    REQUIRE(holes.write(texels, 2, 1));

    std::vector<std::uint8_t> frame;
    engine.setFrameTap([&](std::span<const std::uint8_t> rgba, coney::graphics::Extent size) {
        if (size.width == kWidth && size.height == kHeight) {
            frame.assign(rgba.begin(), rgba.end());
        }
    });
    engine.beginWindowFrame(Rgba{100, 100, 100, 255});
    coney::graphics::SceneCamera camera;
    camera.halfWidth = 0.5F;
    camera.halfHeight = 0.375F;
    camera.nearClip = 1.0F;
    camera.farClip = 100.0F;
    camera.fogStart = 50.0F;
    engine.setSceneCamera(camera);
    coney::graphics::Draw3d scene;
    scene.mesh = *mesh;
    scene.states.fog = false;
    engine.drawScene(scene, {});

    // The states in force before the effects, as the world pass leaves them: fogged in green, blended by alpha, the
    // alpha test at least.
    namespace draw = coney::platform::draw;
    draw::setRenderState(rw::FOGENABLE, 1);
    draw::setRenderState(rw::FOGCOLOR, 0xFF00C800U); // red in the low byte: (0, 200, 0)
    draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
    draw::setRenderState(rw::ZTESTENABLE, 1);
    draw::setRenderState(rw::ZWRITEENABLE, 1);
    draw::setRenderState(rw::VERTEXALPHA, 0);

    // The surface, which sets no depth or fog state (as the water does), at 75 over the last column.
    EffectDraw surface;
    surface.states.cull = coney::graphics::CullFaces::None;
    const auto water = rectangle(240.0F, 0.0F, 320.0F, 120.0F, 75.0F, Rgba{255, 255, 255, 255});
    engine.drawEffect(surface, water, {}, nullptr);

    // The sprites, each setting what a particle sets: depth tested, not written, unfogged, the vertex alpha used.
    const auto sprite = [](EffectBlend blend) {
        EffectDraw made;
        made.states.depthTest = true;
        made.states.depthWrite = false;
        made.states.fog = false;
        made.states.vertexAlpha = true;
        made.states.cull = coney::graphics::CullFaces::None;
        made.states.blend = blend;
        made.restore = coney::graphics::EffectRestore{.fog = true, .blend = true, .depthWrite = true, .cull = true};
        return made;
    };
    engine.drawEffect(sprite(EffectBlend::Alpha), rectangle(0.0F, 0.0F, 80.0F, 120.0F, 10.0F, Rgba{255, 0, 0, 128}), {},
                      nullptr);
    engine.drawEffect(sprite(EffectBlend::Additive),
                      rectangle(80.0F, 0.0F, 160.0F, 120.0F, 10.0F, Rgba{0, 255, 0, 128}), {}, nullptr);
    engine.drawEffect(sprite(EffectBlend::Alpha), rectangle(160.0F, 0.0F, 240.0F, 120.0F, 30.0F, Rgba{0, 0, 255, 255}),
                      {}, nullptr);

    // The texture with holes, its vertex alpha off: its own alpha turns the blending and the alpha test on.
    EffectDraw textured;
    textured.states.vertexAlpha = false;
    textured.states.fog = false;
    textured.states.filter = coney::graphics::TextureFilter::Nearest;
    textured.states.address = coney::graphics::TextureAddress::Clamp;
    engine.drawEffect(textured, rectangle(0.0F, 120.0F, 320.0F, 200.0F, 10.0F, Rgba{255, 255, 255, 255}), {}, &holes);

    // A yellow line through the middle of row 220, as the debug tools' lines set their states.
    EffectDraw line;
    line.shape = coney::graphics::EffectShape::Lines;
    line.states.depthTest = true;
    line.states.depthWrite = false;
    line.states.vertexAlpha = false;
    const std::array<float, 3> from = at(0.0F, 220.5F, 10.0F);
    const std::array<float, 3> to = at(320.0F, 220.5F, 10.0F);
    const std::array<EffectVertex, 2> ends{EffectVertex{from[0], from[1], from[2], Rgba{255, 255, 0, 255}},
                                           EffectVertex{to[0], to[1], to[2], Rgba{255, 255, 0, 255}}};
    engine.drawEffect(line, ends, {}, nullptr);
    engine.present();

    REQUIRE(frame.size() == static_cast<std::size_t>(kWidth) * kHeight * 4);
    CHECK(near(pixelAt(frame, kWidth, 40, 60), Rgba{178, 50, 50, 255}, 2));     // blended
    CHECK(near(pixelAt(frame, kWidth, 120, 60), Rgba{100, 228, 100, 255}, 2));  // added
    CHECK(near(pixelAt(frame, kWidth, 200, 60), Rgba{128, 128, 128, 255}, 2));  // hidden behind the mesh
    CHECK(near(pixelAt(frame, kWidth, 280, 60), Rgba{128, 228, 128, 255}, 2));  // half fogged
    CHECK(near(pixelAt(frame, kWidth, 80, 160), Rgba{255, 0, 0, 255}, 2));      // the texture's red texel
    CHECK(near(pixelAt(frame, kWidth, 240, 160), Rgba{100, 100, 100, 255}, 2)); // its clear one dropped
    CHECK(near(pixelAt(frame, kWidth, 160, 220), Rgba{255, 255, 0, 255}, 2));   // the line

    engine.setFrameTap({});
    assets.removeMesh(*mesh);
    coney::test::destroyQuad(quad);
}
