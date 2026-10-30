// SPDX-License-Identifier: GPL-3.0-or-later
// The headless renderer records a frame's 2D draws and screen passes into its frame description
// (docs/engine/renderer.md#frame-description), which the GL3 renderer then draws. The expected values are Coney's own:
// the description is the renderer's seam, which the original does not have; the 2D pass's mapping is
// graphics::logicalToWindow()'s and the line blend's shift graphics::originalLineShare()'s.
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "effects/motion_blur.h"
#include "graphics/frame_description.h"
#include "graphics/render_device.h"
#include "graphics/screen.h"
#include "platform/motion_blur_pass.h"
#include "platform/render_engine.h"

using coney::graphics::BlurPulse;
using coney::graphics::Draw2d;
using coney::graphics::FrameDescription;
using coney::graphics::FrameStart;
using coney::graphics::kNoFrameTexture;
using coney::graphics::LineBlend;
using coney::graphics::LogicalQuad;
using coney::graphics::LogicalRect;
using coney::graphics::LogicalVertex;
using coney::graphics::Material2d;
using coney::graphics::MotionBlurOverlay;
using coney::graphics::Overlay2dPass;
using coney::graphics::Rgba;
using coney::graphics::ScreenPass;
using coney::graphics::ScreenRect;
using coney::graphics::Shape2d;
using coney::graphics::TextureAddress;
using coney::graphics::TriangleStates;
using coney::graphics::UvRect;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;

namespace {

// The headless renderer, at the original's frame size (which is not 4:3, so the logical screen sits inside it).
std::unique_ptr<RenderEngine> startHeadless() {
    coney::platform::WindowDesc desc;
    desc.width = 640;
    desc.height = 448;
    auto engine = RenderEngine::start(RenderBackend::Null, desc);
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// A texture of a size, standing in for a sprite sheet's; the headless renderer never samples it.
class FakeTexture final : public coney::graphics::Texture {
  public:
    [[nodiscard]] int width() const override { return 16; }
    [[nodiscard]] int height() const override { return 8; }
};

// The overlay pass at `index` of `frame`; fails the test when that pass is not one.
const Overlay2dPass& overlayAt(const FrameDescription& frame, std::size_t index) {
    REQUIRE(index < frame.passes().size());
    REQUIRE(std::holds_alternative<Overlay2dPass>(frame.passes()[index]));
    return std::get<Overlay2dPass>(frame.passes()[index]);
}

// The screen pass at `index` of `frame`; fails the test when that pass is not one.
const ScreenPass& screenAt(const FrameDescription& frame, std::size_t index) {
    REQUIRE(index < frame.passes().size());
    REQUIRE(std::holds_alternative<ScreenPass>(frame.passes()[index]));
    return std::get<ScreenPass>(frame.passes()[index]);
}

// `rect` in logical pixels as the frame description keeps it, mapped onto `onto`.
LogicalRect mapped(const LogicalRect& rect, const ScreenRect& onto) {
    return coney::graphics::logicalToWindow(rect, onto);
}

} // namespace

TEST_CASE("a headless frame records its 2D draws into the frame description", "[platform][frame-description]") {
    // Expected: RenderEngine's 2D drawing as the draw-stream scene draws it (draw_calls_test.cpp): the fill quad of
    // beginFrame(), the logical screen's and the view's mappings by graphics::logicalToWindow(), the alpha test of
    // TriangleStates, and docs/engine/renderer.md#frame-description for what a draw holds.
    auto engine = startHeadless();
    const FakeTexture sheet;
    engine->beginFrame(Rgba{32, 48, 64, 255});
    const ScreenRect screen = engine->logicalViewport();
    const ScreenRect view = engine->viewRect();
    REQUIRE(screen != view); // 640 x 448 is wider than 4:3

    const LogicalQuad quad{10.0F, 20.0F, 100.0F, 40.0F, UvRect{}, Rgba{255, 0, 0, 255}};
    engine->drawQuads(&sheet, std::span<const LogicalQuad>(&quad, 1));
    engine->drawWrappedViewQuads(&sheet, std::span<const LogicalQuad>(&quad, 1));
    const std::array<LogicalVertex, 3> triangle{{
        {100.0F, 100.0F, 0.0F, 0.0F, Rgba{255, 255, 255, 255}},
        {200.0F, 100.0F, 1.0F, 0.0F, Rgba{255, 255, 0, 200}},
        {150.0F, 180.0F, 0.5F, 1.0F, Rgba{255, 0, 255, 100}},
    }};
    engine->drawTriangles(nullptr, triangle, TriangleStates{true, 0.5F});
    const LogicalQuad rect{4.0F, 4.0F, 12.0F, 12.0F, UvRect{}, Rgba{200, 200, 200, 255}};
    engine->drawWindowRects(std::span<const LogicalQuad>(&rect, 1));
    engine->present();

    const FrameDescription& frame = engine->frame();
    CHECK(frame.start() == FrameStart{Rgba{32, 48, 64, 255}, false});
    // One overlay pass: the clear colour's fill, the quad, the wrapped view quad, the triangle, the window rectangle.
    REQUIRE(frame.passes().size() == 1);
    const Overlay2dPass& pass = overlayAt(frame, 0);
    REQUIRE(pass.draws.size() == 5);

    // The fill: flat, the whole logical screen in the clear colour.
    CHECK(pass.draws[0] == Draw2d{Shape2d::Quads, Material2d{}, 0, 1});
    const LogicalQuad& fill = frame.quadsOf(pass.draws[0])[0];
    CHECK(LogicalRect{fill.x, fill.y, fill.width, fill.height} == mapped(LogicalRect{0, 0, 640, 448}, screen));
    CHECK(fill.colour == Rgba{32, 48, 64, 255});

    // The quad on the logical screen, with the sheet's texture, clamped and with no alpha test of its own.
    const auto sheetId = pass.draws[1].material.texture;
    CHECK(sheetId != kNoFrameTexture);
    CHECK(frame.textureOf(sheetId) == &sheet);
    CHECK(pass.draws[1].material.address == TextureAddress::Clamp);
    CHECK_FALSE(pass.draws[1].material.alphaRef.has_value());
    const LogicalQuad& onScreen = frame.quadsOf(pass.draws[1])[0];
    CHECK(LogicalRect{onScreen.x, onScreen.y, onScreen.width, onScreen.height} ==
          mapped(LogicalRect{10, 20, 100, 40}, screen));

    // The view quad over the whole view, wrapped, the same texture by the same id.
    CHECK(pass.draws[2].material.texture == sheetId);
    CHECK(pass.draws[2].material.address == TextureAddress::Wrap);
    const LogicalQuad& onView = frame.quadsOf(pass.draws[2])[0];
    CHECK(LogicalRect{onView.x, onView.y, onView.width, onView.height} == mapped(LogicalRect{10, 20, 100, 40}, view));

    // The triangle: flat, wrapped, alpha tested at a half.
    CHECK(pass.draws[3].shape == Shape2d::Triangles);
    CHECK(pass.draws[3].count == 3);
    CHECK(pass.draws[3].material.texture == kNoFrameTexture);
    CHECK(pass.draws[3].material.address == TextureAddress::Wrap);
    CHECK(pass.draws[3].material.alphaRef == 0.5F);

    // The window rectangle as given.
    const LogicalQuad& windowRect = frame.quadsOf(pass.draws[4])[0];
    CHECK(windowRect.x == 4.0F);
    CHECK(windowRect.width == 12.0F);
}

TEST_CASE("the screen passes are recorded between a frame's overlay passes", "[platform][frame-description]") {
    // Expected: RenderEngine::blurScreen and present()'s line blend (src/platform/render_engine.h) over viewRect(), the
    // shift graphics::originalLineShare() (docs/research/rendering.md#output); the order of
    // docs/engine/renderer.md#frame-description.
    auto engine = startHeadless();
    engine->setLineBlend(true);
    engine->beginFrame(Rgba{0, 0, 0, 255}); // black: no fill
    const LogicalQuad quad{0.0F, 0.0F, 64.0F, 64.0F, UvRect{}, Rgba{255, 255, 255, 255}};
    engine->drawQuads(nullptr, std::span<const LogicalQuad>(&quad, 1));
    engine->blurScreen(2, 0.125F, 0.25F);
    engine->drawQuads(nullptr, std::span<const LogicalQuad>(&quad, 1));
    engine->present();

    // Overlay, the blur pulse over the whole view, overlay, then the line blend at the present.
    const FrameDescription& frame = engine->frame();
    const ScreenRect view = engine->viewRect();
    REQUIRE(frame.passes().size() == 4);
    CHECK(overlayAt(frame, 0).draws.size() == 1);
    CHECK(screenAt(frame, 1) == ScreenPass{view, BlurPulse{2, 0.125F, 0.25F}});
    CHECK(overlayAt(frame, 2).draws.size() == 1);
    const float shift = coney::graphics::originalLineShare(engine->logicalViewport(), view);
    CHECK(screenAt(frame, 3) == ScreenPass{view, LineBlend{shift}});

    // Without the line blend, the next frame ends with its last overlay.
    engine->setLineBlend(false);
    engine->beginFrame(Rgba{0, 0, 0, 255});
    engine->drawQuads(nullptr, std::span<const LogicalQuad>(&quad, 1));
    engine->present();
    CHECK(engine->frame().passes().size() == 1);
}

TEST_CASE("each motion blur pass records its own kept copy", "[platform][frame-description]") {
    // Expected: MotionBlurPass (src/platform/motion_blur_pass.h): its pass over the whole view (viewRect(), the 640 x
    // 448 frame here), in the blur's colour, with a copy of its own kept from frame to frame.
    auto engine = startHeadless();
    const ScreenRect view{0, 0, 640, 448};
    coney::platform::MotionBlurPass first;
    coney::platform::MotionBlurPass second;
    engine->beginFrame(Rgba{0, 0, 0, 255});
    first.apply(*engine, coney::effects::MotionBlur::Colour{200, 210, 220, 96});
    second.apply(*engine, coney::effects::MotionBlur::Colour{255, 255, 255, 0});
    engine->present();

    const FrameDescription& frame = engine->frame();
    REQUIRE(frame.passes().size() == 2);
    const ScreenPass& firstPass = screenAt(frame, 0);
    const ScreenPass& secondPass = screenAt(frame, 1);
    CHECK(firstPass.area == view);
    REQUIRE(std::holds_alternative<MotionBlurOverlay>(firstPass.effect));
    REQUIRE(std::holds_alternative<MotionBlurOverlay>(secondPass.effect));
    const auto& firstBlur = std::get<MotionBlurOverlay>(firstPass.effect);
    const auto& secondBlur = std::get<MotionBlurOverlay>(secondPass.effect);
    CHECK(firstBlur.colour == Rgba{200, 210, 220, 96});
    // A strength of 0 is recorded too: it tells the renderer to forget the kept frame.
    CHECK(secondBlur.colour == Rgba{255, 255, 255, 0});
    CHECK(firstBlur.copy != secondBlur.copy);

    // The same pass keeps its copy from frame to frame.
    engine->beginFrame(Rgba{0, 0, 0, 255});
    first.apply(*engine, coney::effects::MotionBlur::Colour{200, 210, 220, 96});
    engine->present();
    REQUIRE(engine->frame().passes().size() == 1);
    const ScreenPass& again = screenAt(engine->frame(), 0);
    REQUIRE(std::holds_alternative<MotionBlurOverlay>(again.effect));
    CHECK(std::get<MotionBlurOverlay>(again.effect).copy == firstBlur.copy);
}
