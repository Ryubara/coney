// SPDX-License-Identifier: GPL-3.0-or-later
// The frame description built headless, with no renderer (docs/engine/renderer.md#frame-description). The expected
// values are Coney's own: the description is the renderer's seam, which the original does not have; the mapping from
// logical to window pixels is graphics::logicalToWindow()'s, tested in screen_test.cpp.
#include "graphics/frame_description.h"

#include <array>
#include <cstddef>
#include <span>
#include <variant>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/render_device.h"
#include "graphics/screen.h"

using Catch::Approx;
using coney::graphics::BlurPulse;
using coney::graphics::Draw2d;
using coney::graphics::FrameDescription;
using coney::graphics::FramePass;
using coney::graphics::FrameStart;
using coney::graphics::kNoFrameTexture;
using coney::graphics::LineBlend;
using coney::graphics::LogicalQuad;
using coney::graphics::LogicalVertex;
using coney::graphics::Material2d;
using coney::graphics::MotionBlurOverlay;
using coney::graphics::Overlay2dPass;
using coney::graphics::Rgba;
using coney::graphics::ScreenPass;
using coney::graphics::ScreenRect;
using coney::graphics::ScreenVertex;
using coney::graphics::Shape2d;
using coney::graphics::TextureAddress;
using coney::graphics::UvRect;

namespace {

// A texture of a size, standing in for a sprite sheet's.
class FakeTexture final : public coney::graphics::Texture {
  public:
    [[nodiscard]] int width() const override { return 16; }
    [[nodiscard]] int height() const override { return 8; }
};

// The overlay pass at `index` of `frame`; fails the test when that pass is not one.
const Overlay2dPass& overlayAt(const FrameDescription& frame, std::size_t index) {
    REQUIRE(index < frame.passes().size());
    const FramePass& pass = frame.passes()[index];
    REQUIRE(std::holds_alternative<Overlay2dPass>(pass));
    return std::get<Overlay2dPass>(pass);
}

// The screen pass at `index` of `frame`; fails the test when that pass is not one.
const ScreenPass& screenAt(const FrameDescription& frame, std::size_t index) {
    REQUIRE(index < frame.passes().size());
    const FramePass& pass = frame.passes()[index];
    REQUIRE(std::holds_alternative<ScreenPass>(pass));
    return std::get<ScreenPass>(pass);
}

// A logical screen placed in a 1280 x 720 window, as graphics::fitLogicalScreen() puts it.
constexpr ScreenRect kPillarbox{160, 0, 960, 720};

} // namespace

TEST_CASE("a frame description starts empty with its clear colour", "[graphics][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description; FrameDescription::begin's contract
    // (src/graphics/frame_description.h): a new frame holds its start and nothing else.
    FrameDescription frame;
    frame.begin(FrameStart{Rgba{10, 20, 30, 255}, false});
    CHECK(frame.start() == FrameStart{Rgba{10, 20, 30, 255}, false});
    CHECK(frame.passes().empty());
    CHECK(frame.quads().empty());
    CHECK(frame.vertices().empty());
    CHECK(frame.textureCount() == 0);
}

TEST_CASE("a frame description maps logical quads onto the window and keeps their order",
          "[graphics][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description; graphics::logicalToWindow() (src/graphics/screen.cpp): x *
    // 960 / 640 + 160 and y * 720 / 448 for the logical screen fitLogicalScreen() places in a 1280 x 720 window
    // (tests/graphics/screen_test.cpp).
    FrameDescription frame;
    frame.begin(FrameStart{});
    const std::array<LogicalQuad, 2> quads{{
        {0.0F, 0.0F, 640.0F, 448.0F, UvRect{}, Rgba{1, 2, 3, 4}},
        {320.0F, 224.0F, 64.0F, 32.0F, UvRect{0.25F, 0.0F, 0.75F, 1.0F}, Rgba{5, 6, 7, 8}},
    }};
    frame.addQuads(Material2d{}, quads, kPillarbox);

    // One overlay pass of one draw of both quads, in window pixels: the 640 x 448 screen is 960 x 720 from x 160.
    REQUIRE(frame.passes().size() == 1);
    const Overlay2dPass& pass = overlayAt(frame, 0);
    REQUIRE(pass.draws.size() == 1);
    CHECK(pass.draws[0] == Draw2d{Shape2d::Quads, Material2d{}, 0, 2});
    const std::span<const LogicalQuad> mapped = frame.quadsOf(pass.draws[0]);
    REQUIRE(mapped.size() == 2);
    CHECK(mapped[0].x == 160.0F);
    CHECK(mapped[0].y == 0.0F);
    CHECK(mapped[0].width == 960.0F);
    CHECK(mapped[0].height == 720.0F);
    CHECK(mapped[1].x == 160.0F + 480.0F);
    CHECK(mapped[1].y == Approx(360.0F));
    CHECK(mapped[1].width == 96.0F);
    CHECK(mapped[1].height == Approx(32.0F * 720.0F / 448.0F));
    // Texture coordinates and colours as given.
    CHECK(mapped[1].uv == UvRect{0.25F, 0.0F, 0.75F, 1.0F});
    CHECK(mapped[1].colour == Rgba{5, 6, 7, 8});

    // Window quads are kept as given, after the first draw, in the same pass.
    const LogicalQuad rect{4.0F, 5.0F, 6.0F, 7.0F, UvRect{}, Rgba{9, 9, 9, 255}};
    frame.addWindowQuads(Material2d{}, std::span<const LogicalQuad>(&rect, 1));
    REQUIRE(pass.draws.size() == 2);
    CHECK(pass.draws[1] == Draw2d{Shape2d::Quads, Material2d{}, 2, 1});
    REQUIRE(frame.quadsOf(pass.draws[1]).size() == 1);
    CHECK(frame.quadsOf(pass.draws[1])[0].x == 4.0F);
    CHECK(frame.quadsOf(pass.draws[1])[0].height == 7.0F);
}

TEST_CASE("a frame description drops a partial triangle and adds nothing for no shape",
          "[graphics][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description; RenderDevice::drawTriangles' contract
    // (src/graphics/render_device.h): three corners a triangle, a remainder ignored; the corners mapped by
    // graphics::logicalToWindow() as in the quads' test.
    FrameDescription frame;
    frame.begin(FrameStart{});
    const std::array<LogicalVertex, 5> corners{{
        {0.0F, 0.0F, 0.0F, 0.0F, Rgba{255, 0, 0, 255}},
        {640.0F, 0.0F, 1.0F, 0.0F, Rgba{0, 255, 0, 255}},
        {0.0F, 448.0F, 0.0F, 1.0F, Rgba{0, 0, 255, 255}},
        {1.0F, 1.0F, 0.0F, 0.0F, Rgba{}},
        {2.0F, 2.0F, 0.0F, 0.0F, Rgba{}},
    }};
    Material2d tested;
    tested.alphaRef = 0.5F;
    frame.addTriangles(tested, corners, kPillarbox);
    const Overlay2dPass& pass = overlayAt(frame, 0);
    REQUIRE(pass.draws.size() == 1);
    CHECK(pass.draws[0] == Draw2d{Shape2d::Triangles, tested, 0, 3});
    const std::span<const ScreenVertex> mapped = frame.verticesOf(pass.draws[0]);
    REQUIRE(mapped.size() == 3);
    CHECK(mapped[0] == ScreenVertex{160.0F, 0.0F, 0.0F, 0.0F, Rgba{255, 0, 0, 255}});
    CHECK(mapped[1] == ScreenVertex{1120.0F, 0.0F, 1.0F, 0.0F, Rgba{0, 255, 0, 255}});
    CHECK(mapped[2] == ScreenVertex{160.0F, 720.0F, 0.0F, 1.0F, Rgba{0, 0, 255, 255}});

    // Two corners, no quads: nothing more.
    frame.addTriangles(tested, std::span(corners).first(2), kPillarbox);
    frame.addQuads(Material2d{}, {}, kPillarbox);
    frame.addWindowQuads(Material2d{}, {});
    CHECK(frame.passes().size() == 1);
    CHECK(pass.draws.size() == 1);
    CHECK(frame.vertices().size() == 3);
}

TEST_CASE("a frame description gives each texture one id", "[graphics][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description; FrameDescription::texture's contract
    // (src/graphics/frame_description.h): one id per texture per frame, kNoFrameTexture for none, the table emptied by
    // begin().
    FrameDescription frame;
    frame.begin(FrameStart{});
    const FakeTexture first;
    const FakeTexture second;
    CHECK(frame.texture(nullptr) == kNoFrameTexture);
    const auto firstId = frame.texture(&first);
    const auto secondId = frame.texture(&second);
    CHECK(firstId != kNoFrameTexture);
    CHECK(secondId != kNoFrameTexture);
    CHECK(firstId != secondId);
    CHECK(frame.texture(&first) == firstId);
    CHECK(frame.textureCount() == 2);
    CHECK(frame.textureOf(firstId) == &first);
    CHECK(frame.textureOf(secondId) == &second);
    CHECK(frame.textureOf(kNoFrameTexture) == nullptr);
    CHECK(frame.textureOf(secondId + 1) == nullptr);

    // A new frame starts its table afresh.
    frame.begin(FrameStart{});
    CHECK(frame.textureCount() == 0);
    CHECK(frame.textureOf(firstId) == nullptr);
}

TEST_CASE("a screen pass splits a frame's overlay passes", "[graphics][frame-description]") {
    // Expected: docs/engine/renderer.md#frame-description; FrameDescription::addScreenPass's contract
    // (src/graphics/frame_description.h): passes kept in order, the next 2D draw after a screen pass starting a new
    // overlay pass.
    FrameDescription frame;
    frame.begin(FrameStart{});
    const LogicalQuad quad{0.0F, 0.0F, 10.0F, 10.0F, UvRect{}, Rgba{}};
    Material2d wrapped;
    wrapped.address = TextureAddress::Wrap;
    frame.addQuads(Material2d{}, std::span<const LogicalQuad>(&quad, 1), kPillarbox);
    frame.addQuads(wrapped, std::span<const LogicalQuad>(&quad, 1), kPillarbox);
    const ScreenRect view{0, 0, 1280, 720};
    frame.addScreenPass(ScreenPass{view, BlurPulse{2, 0.5F, 0.25F}});
    frame.addQuads(Material2d{}, std::span<const LogicalQuad>(&quad, 1), kPillarbox);
    frame.addScreenPass(ScreenPass{view, MotionBlurOverlay{3, Rgba{200, 210, 220, 96}}});
    frame.addScreenPass(ScreenPass{view, LineBlend{1.0F / 448.0F}});

    // Overlay (two draws), blur pulse, overlay (one draw), motion blur, line blend.
    REQUIRE(frame.passes().size() == 5);
    CHECK(overlayAt(frame, 0).draws.size() == 2);
    CHECK(overlayAt(frame, 0).draws[1].material.address == TextureAddress::Wrap);
    CHECK(screenAt(frame, 1) == ScreenPass{view, BlurPulse{2, 0.5F, 0.25F}});
    CHECK(overlayAt(frame, 2).draws.size() == 1);
    CHECK(overlayAt(frame, 2).draws[0].first == 2);
    CHECK(screenAt(frame, 3) == ScreenPass{view, MotionBlurOverlay{3, Rgba{200, 210, 220, 96}}});
    CHECK(screenAt(frame, 4) == ScreenPass{view, LineBlend{1.0F / 448.0F}});

    // The next frame forgets them.
    frame.begin(FrameStart{Rgba{1, 1, 1, 255}, true});
    CHECK(frame.passes().empty());
    CHECK(frame.quads().empty());
    CHECK(frame.start().wholeWindow);
}
