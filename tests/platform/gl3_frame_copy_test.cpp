// SPDX-License-Identifier: GPL-3.0-or-later
// The GL3 renderer's frame copies (the screen passes' copies of the view, docs/engine/gpu-renderer.md#screen-passes)
// leave librw drawing with the textures it believes are bound. Found by comparing the screen passes with the GPU
// renderer: the copy's texture stayed bound behind librw's back, so the next draw with no texture sampled the copy.
// No game data: every rectangle is made here. The case needs a display and an OpenGL 3.3 context, and skips without.
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "effects/motion_blur.h"
#include "graphics/render_device.h"
#include "platform/motion_blur_pass.h"
#include "platform/render_engine.h"
#include "support/gpu_capture.h"

using coney::graphics::LogicalQuad;
using coney::graphics::Rgba;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;

TEST_CASE("a flat quad the GL3 renderer draws after copying the frame is flat, not the copy",
          "[render][renderer][window][screen-passes]") {
    // Expected: ../engine/gpu-renderer.md#screen-passes, a quad with no texture is drawn in its colour (librw's white
    // texture). Before, draw::copyFrameToRaster left the copy's texture bound, so the next frame's flat quads were
    // drawn in their colour times the motion blur's kept frame.
    constexpr int kWidth = 320;
    constexpr int kHeight = 240;
    coney::platform::WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = kWidth;
    desc.height = kHeight;
    auto started = RenderEngine::start(RenderBackend::OpenGl, desc);
    if (!started) {
        SKIP("GL3 does not start here: " << started.error().message);
    }
    RenderEngine& engine = **started;
    std::vector<std::uint8_t> frame;
    engine.setFrameTap([&frame](std::span<const std::uint8_t> rgba, coney::graphics::Extent size) {
        if (size.width == kWidth && size.height == kHeight) {
            frame.assign(rgba.begin(), rgba.end());
        }
    });
    auto pass = std::make_unique<coney::platform::MotionBlurPass>();

    // Frame 1: red with a black stripe, kept by the motion blur's first frame (a copy, nothing laid).
    engine.beginFrame(Rgba{0, 0, 0, 255});
    const std::array<LogicalQuad, 2> picture{{{0.0F, 0.0F, kWidth, kHeight, {}, Rgba{220, 40, 40, 255}},
                                              {0.0F, 100.0F, kWidth, 10.0F, {}, Rgba{10, 10, 10, 255}}}};
    engine.drawWindowRects(picture);
    pass->apply(engine, coney::effects::MotionBlur::Colour{255, 255, 255, 128});
    engine.present();

    // Frame 2: one flat grey quad over the whole window, no blur.
    engine.beginFrame(Rgba{0, 0, 0, 255});
    const std::array<LogicalQuad, 1> flat{{{0.0F, 0.0F, kWidth, kHeight, {}, Rgba{200, 200, 200, 255}}}};
    engine.drawWindowRects(flat);
    engine.present();

    REQUIRE(frame.size() == static_cast<std::size_t>(kWidth) * kHeight * 4);
    CHECK(coney::test::near(coney::test::pixelAt(frame, kWidth, 20, 30), Rgba{200, 200, 200, 255}, 1));  // the red
    CHECK(coney::test::near(coney::test::pixelAt(frame, kWidth, 20, 105), Rgba{200, 200, 200, 255}, 1)); // the stripe
    pass.reset();
    engine.setFrameTap({});
}
