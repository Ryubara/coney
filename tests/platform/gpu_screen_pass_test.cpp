// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's screen passes (docs/engine/gpu-renderer.md#screen-passes, item 20 of
// docs/engine/renderer.md#order): the blur pulse, the line blend and the motion blur with its kept frame, each drawn
// over a Coney-made picture by the GPU renderer and by the GL3 renderer on hidden windows, read back and compared pixel
// by pixel by the cross-renderer tolerance (tests/pictures/scenes.toml: 4 of 255, at most 0.2 % of the pixels over). No
// game data: every rectangle is made here. The cases need SDL's video, an OpenGL 3.3 context and a GPU device, and skip
// where there is none.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <span>
#include <utility>
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
using coney::platform::WindowDesc;
using coney::test::near;
using coney::test::pixelAt;

namespace {

// The window: 4:3, so the 3D view and the frame tap's logical screen are the whole window on both renderers.
constexpr int kWidth = 320;
constexpr int kHeight = 240;
// The cross-renderer tolerance (tests/pictures/scenes.toml, docs/engine/renderer.md#comparing).
constexpr int kChannelTolerance = 4;
constexpr double kMaxShare = 0.002;

// The test picture: four coloured bands across, cut by thin vertical bars and one-pixel rows, so every pass has edges
// to soften. `shift` moves the bars, for a second frame that differs from the first.
void drawPicture(RenderEngine& engine, float shift) {
    std::vector<LogicalQuad> quads;
    const Rgba bands[] = {{220, 40, 40, 255}, {40, 200, 60, 255}, {40, 60, 220, 255}, {230, 210, 40, 255}};
    for (int i = 0; i < 4; ++i) {
        quads.push_back(LogicalQuad{0.0F, static_cast<float>(i * 60), kWidth, 60.0F, {}, bands[i]});
    }
    for (int x = 0; x < kWidth; x += 40) {
        quads.push_back(LogicalQuad{static_cast<float>(x) + shift, 0.0F, 8.0F, kHeight, {}, Rgba{250, 250, 250, 255}});
    }
    for (int y = 3; y < kHeight; y += 7) {
        quads.push_back(LogicalQuad{0.0F, static_cast<float>(y), kWidth, 1.0F, {}, Rgba{10, 10, 10, 255}});
    }
    engine.drawWindowRects(quads);
}

// Runs `frames` frames on `backend` in a hidden window, each begun black, drawn by `draw` (given the frame's number)
// and presented, and returns the last frame as the frame tap reads it (RGBA, rows top first); empty when the renderer
// does not start here. `draw` is called once more with frame -1 and no frame begun, before the renderer stops, to let
// go of what it holds of it (a motion blur's kept copy).
std::vector<std::uint8_t> drawOn(RenderBackend backend, int frames,
                                 const std::function<void(RenderEngine&, int)>& draw) {
    WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = kWidth;
    desc.height = kHeight;
    auto started = RenderEngine::start(backend, desc);
    if (!started) {
        return {};
    }
    RenderEngine& engine = **started;
    engine.setVsync(false);
    std::vector<std::uint8_t> frame;
    engine.setFrameTap([&frame](std::span<const std::uint8_t> rgba, coney::graphics::Extent size) {
        if (size.width == kWidth && size.height == kHeight) {
            frame.assign(rgba.begin(), rgba.end());
        }
    });
    for (int i = 0; i < frames; ++i) {
        engine.beginFrame(Rgba{0, 0, 0, 255});
        draw(engine, i);
        engine.present();
    }
    draw(engine, -1);
    engine.setFrameTap({});
    return frame;
}

// The pixels of `a` and `b` (the same size) with a channel more than the tolerance apart.
std::size_t pixelsOver(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    std::size_t over = 0;
    for (std::size_t i = 0; i + 3 < a.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c])) > kChannelTolerance) {
                ++over;
                break;
            }
        }
    }
    return over;
}

// Draws the same frames with GL3 and with the GPU renderer and checks the GPU renderer's last frame against GL3's by
// the cross-renderer tolerance; returns both (GL3's first), empty when either renderer does not start here.
std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
compareRenderers(int frames, const std::function<void(RenderEngine&, int)>& draw) {
    std::vector<std::uint8_t> gl3 = drawOn(RenderBackend::OpenGl, frames, draw);
    std::vector<std::uint8_t> gpu = drawOn(RenderBackend::Gpu, frames, draw);
    if (gl3.empty() || gpu.empty()) {
        return {};
    }
    REQUIRE(gl3.size() == gpu.size());
    const std::size_t over = pixelsOver(gl3, gpu);
    INFO(over << " pixels over " << kChannelTolerance << " of 255");
    CHECK(static_cast<double>(over) <= kMaxShare * kWidth * kHeight);
    return {std::move(gl3), std::move(gpu)};
}

} // namespace

TEST_CASE("the GPU renderer draws the blur pulse as GL3 does", "[render][renderer][window][screen-passes]") {
    // Expected: ../engine/gpu-renderer.md#screen-passes, the blur pass of the original's device (0x00193d80, drawn by
    // the GL3 renderer's drawBlurPulse()): the view replaced by its half-size image passed over itself and stretched
    // back, so the GPU renderer's frame matches GL3's within the cross-renderer tolerance, and the one-pixel rows of
    // the picture are blurred away (a row's pixel is no longer near black).
    if (!coney::test::gpuHere()) {
        SKIP("no display or no GPU device here");
    }
    const auto [gl3, gpu] = compareRenderers(1, [](RenderEngine& engine, int frame) {
        if (frame >= 0) {
            drawPicture(engine, 0.0F);
            engine.blurScreen(4, 0.004F, 0.008F);
        }
    });
    if (gl3.empty()) {
        SKIP("GL3 or the GPU renderer does not start here");
    }
    CHECK_FALSE(near(pixelAt(gpu, kWidth, 20, 3), Rgba{10, 10, 10, 255}, 20));
}

TEST_CASE("the GPU renderer draws the line blend as GL3 does", "[render][renderer][window][screen-passes]") {
    // Expected: ../engine/gpu-renderer.md#screen-passes, the PS2 video output's softening (drawn by the GL3 renderer's
    // drawLineBlend(), docs/research/rendering.md#output): each line half itself and half the line one original line
    // below, so the GPU renderer's frame matches GL3's, and the row above a band's edge takes some of the band below.
    if (!coney::test::gpuHere()) {
        SKIP("no display or no GPU device here");
    }
    const auto [gl3, gpu] = compareRenderers(1, [](RenderEngine& engine, int frame) {
        if (frame >= 0) {
            engine.setLineBlend(true);
            drawPicture(engine, 0.0F);
        }
    });
    if (gl3.empty()) {
        SKIP("GL3 or the GPU renderer does not start here");
    }
    // Row 119 is green (40, 200, 60) and row 120 blue (40, 60, 220); one original line is 240 / 448 of a row, so row
    // 119 becomes about (40, 157, 103).
    const Rgba edge = pixelAt(gpu, kWidth, 20, 119);
    CHECK(edge.g < 190);
    CHECK(edge.b > 80);
}

TEST_CASE("the GPU renderer draws the motion blur with its kept frame as GL3 does",
          "[render][renderer][window][screen-passes]") {
    // Expected: ../engine/gpu-renderer.md#screen-passes, Coney's motion blur (platform/motion_blur_pass.h, drawn by the
    // GL3 renderer's drawMotionBlur()): the first frame is only kept; each later frame has the kept frame laid over it
    // in the blur's colour at its strength, then is kept itself. Over three frames whose bars move, the GPU renderer's
    // frame matches GL3's, and where the last frame has a band but the first a bar, the bar shows through.
    if (!coney::test::gpuHere()) {
        SKIP("no display or no GPU device here");
    }
    // A pass of its own for each renderer, made on the first frame and let go of before the renderer stops.
    std::unique_ptr<coney::platform::MotionBlurPass> pass;
    const auto [gl3, gpu] = compareRenderers(3, [&pass](RenderEngine& engine, int frame) {
        if (frame < 0) {
            pass.reset();
            return;
        }
        if (frame == 0) {
            pass = std::make_unique<coney::platform::MotionBlurPass>();
        }
        drawPicture(engine, static_cast<float>(frame) * 12.0F);
        pass->apply(engine, coney::effects::MotionBlur::Colour{255, 230, 200, 128});
    });
    if (gl3.empty()) {
        SKIP("GL3 or the GPU renderer does not start here");
    }
    // Column 4 of the red band: the first frame's bar (x 0 to 8) was there, the last frame's is at 24 to 32. A quarter
    // of the first frame's white is left in it, tinted by the blur's colour.
    const Rgba ghost = pixelAt(gpu, kWidth, 4, 30);
    CHECK(ghost.g > 60);
}
