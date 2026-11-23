// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's screen passes as copies and draws, headless (docs/engine/gpu-renderer.md#screen-passes): the blur
// pulse, the line blend and the motion blur. What they must match is the GL3 renderer's drawing of the same passes
// (src/platform/gl3_screen_passes.cpp), the picture the GPU renderer is compared with; the blur pulse's steps are the
// original's blur pass (device slot +0x108, 0x00193d80 RwDevice_BlurPass, docs/research/graphics.md#blur-pulse and
// #motion-blur): the half-size image drawn into the top-left quarter, the passes with one edge moved in by half the
// offset (the sign flipping each pass), and the image stretched back inset by one texel, opaque.
#include "graphics/screen_pass_steps.h"

#include <cstddef>
#include <variant>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/frame_description.h"
#include "graphics/overlay_batch.h"

using coney::graphics::AlphaRange;
using coney::graphics::kStartAlphaRef;
using coney::graphics::Rgba;
using coney::graphics::ScreenCopyStep;
using coney::graphics::ScreenDrawStep;
using coney::graphics::ScreenPass;
using coney::graphics::ScreenRect;
using coney::graphics::ScreenSlot;
using coney::graphics::ScreenStep;

namespace {

// The view of a 640 x 448 window, the original's frame size.
constexpr ScreenRect kView{0, 0, 640, 448};
// The alpha test every screen quad meets on GL3: librw's after its start.
constexpr AlphaRange kQuadTest{kStartAlphaRef, 1000.0F};

// Step `i` as a copy; fails the test when it is not one.
ScreenCopyStep copyAt(const std::vector<ScreenStep>& steps, std::size_t i) {
    REQUIRE(i < steps.size());
    const auto* copy = std::get_if<ScreenCopyStep>(&steps[i]);
    REQUIRE(copy != nullptr);
    return *copy;
}

// Step `i` as a draw; fails the test when it is not one.
ScreenDrawStep drawAt(const std::vector<ScreenStep>& steps, std::size_t i) {
    REQUIRE(i < steps.size());
    const auto* draw = std::get_if<ScreenDrawStep>(&steps[i]);
    REQUIRE(draw != nullptr);
    return *draw;
}

// Whether `draw`'s corners are the rectangle (`x`, `y`, `w`, `h`) clockwise from its top left with texture coordinates
// (`u0`, `v0`) to (`u1`, `v1`), as the GL3 renderer's drawWindowQuads() makes a quad's corners.
void checkQuad(const ScreenDrawStep& draw, float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
    using Catch::Approx;
    const auto& c = draw.corners;
    CHECK(c[0].x == Approx(x));
    CHECK(c[0].y == Approx(y));
    CHECK(c[1].x == Approx(x + w));
    CHECK(c[1].y == Approx(y));
    CHECK(c[2].x == Approx(x + w));
    CHECK(c[2].y == Approx(y + h));
    CHECK(c[3].x == Approx(x));
    CHECK(c[3].y == Approx(y + h));
    CHECK(c[0].u == Approx(u0));
    CHECK(c[0].v == Approx(v0));
    CHECK(c[1].u == Approx(u1));
    CHECK(c[1].v == Approx(v0));
    CHECK(c[2].u == Approx(u1));
    CHECK(c[2].v == Approx(v1));
    CHECK(c[3].u == Approx(u0));
    CHECK(c[3].v == Approx(v1));
}

} // namespace

TEST_CASE("the blur pulse copies the view, halves it, passes it over itself and stretches it back",
          "[graphics][screen-passes]") {
    // Expected: 0x00193d80 RwDevice_BlurPass (docs/research/graphics.md#motion-blur, the blur pass): the source drawn
    // at half size, two passes of the half-size image onto itself, then stretched back over the whole view inset by
    // one texel (0x0050ce60), opaque; as the GL3 renderer's drawBlurPulse() draws it, each draw reading a copy made
    // just before. The offsets are UV units of the original's 512 x 256 texture, whose half-size image is 320 x 224,
    // so a step is offset / 2 scaled by 512 / 320 and 256 / 224.
    const coney::graphics::BlurPulse blur{2, 0.01F, 0.02F};
    const std::vector<ScreenStep> steps = coney::graphics::blurPulseSteps(blur, kView);
    REQUIRE(steps.size() == 8);
    const Rgba white{255, 255, 255, 255};

    CHECK(copyAt(steps, 0) == ScreenCopyStep{ScreenSlot::View, kView});
    const ScreenDrawStep half = drawAt(steps, 1);
    CHECK(half.from == ScreenSlot::View);
    CHECK(half.opaque);
    CHECK(half.alpha == kQuadTest);
    CHECK(half.corners[0].colour == white);
    checkQuad(half, 0, 0, 320, 224, 0, 0, 1, 1);

    const float stepU = 0.01F / 2.0F * (512.0F / 320.0F);
    const float stepV = 0.02F / 2.0F * (256.0F / 224.0F);
    CHECK(copyAt(steps, 2) == ScreenCopyStep{ScreenSlot::Half, ScreenRect{0, 0, 320, 224}});
    const ScreenDrawStep first = drawAt(steps, 3);
    CHECK(first.from == ScreenSlot::Half);
    CHECK(first.opaque);
    checkQuad(first, 0, 0, 320, 224, stepU, stepV, 1, 1);
    CHECK(copyAt(steps, 4) == ScreenCopyStep{ScreenSlot::Half, ScreenRect{0, 0, 320, 224}});
    checkQuad(drawAt(steps, 5), 0, 0, 320, 224, 0, 0, 1.0F - stepU, 1.0F - stepV);

    CHECK(copyAt(steps, 6) == ScreenCopyStep{ScreenSlot::Half, ScreenRect{0, 0, 320, 224}});
    const ScreenDrawStep whole = drawAt(steps, 7);
    CHECK(whole.from == ScreenSlot::Half);
    CHECK(whole.opaque);
    checkQuad(whole, 0, 0, 640, 448, 1.0F / 320.0F, 1.0F / 224.0F, 1.0F - (1.0F / 320.0F), 1.0F - (1.0F / 224.0F));
}

TEST_CASE("the blur pulse covers a view away from the window's corner and has no steps for a sliver",
          "[graphics][screen-passes]") {
    // Expected: src/platform/gl3_screen_passes.cpp, the GL3 renderer's drawBlurPulse(): the half-size image goes into
    // the view's own top-left quarter (the whole picture the 3D view fills, which in a wide window starts left of the
    // logical screen), its size halved rounding down; a view under 2 pixels either way draws nothing. With no passes,
    // the image is copied once and stretched back.
    const ScreenRect view{10, 20, 101, 51};
    const std::vector<ScreenStep> steps =
        coney::graphics::blurPulseSteps(coney::graphics::BlurPulse{0, 0.0F, 0.0F}, view);
    REQUIRE(steps.size() == 4);
    CHECK(copyAt(steps, 0) == ScreenCopyStep{ScreenSlot::View, view});
    checkQuad(drawAt(steps, 1), 10, 20, 50, 25, 0, 0, 1, 1);
    CHECK(copyAt(steps, 2) == ScreenCopyStep{ScreenSlot::Half, ScreenRect{10, 20, 50, 25}});
    checkQuad(drawAt(steps, 3), 10, 20, 101, 51, 1.0F / 50.0F, 1.0F / 25.0F, 1.0F - (1.0F / 50.0F),
              1.0F - (1.0F / 25.0F));

    CHECK(
        coney::graphics::blurPulseSteps(coney::graphics::BlurPulse{4, 0.01F, 0.01F}, ScreenRect{0, 0, 1, 448}).empty());
    CHECK(
        coney::graphics::blurPulseSteps(coney::graphics::BlurPulse{4, 0.01F, 0.01F}, ScreenRect{0, 0, 640, 1}).empty());

    // A screen pass of the blur pulse is these steps.
    const ScreenPass pass{kView, coney::graphics::BlurPulse{2, 0.01F, 0.02F}};
    const coney::graphics::ScreenSteps fromPass = coney::graphics::screenStepsOf(pass, false);
    CHECK(fromPass.steps == coney::graphics::blurPulseSteps(coney::graphics::BlurPulse{2, 0.01F, 0.02F}, kView));
    CHECK_FALSE(fromPass.forgetKept);
}

TEST_CASE("the line blend lays the view's copy over itself at half strength one original line down",
          "[graphics][screen-passes]") {
    // Expected: src/platform/gl3_screen_passes.cpp, the GL3 renderer's drawLineBlend()
    // (docs/research/rendering.md#output): the view copied, then laid over itself with alpha 128 (PMODE's ALP 0x80),
    // its texture coordinates moved down by the line's share of the view (DISPFB2's DBY 1), blended by alpha, sampled
    // linearly and clamped past the bottom edge.
    const float shift = 1.0F / 448.0F;
    const ScreenPass pass{kView, coney::graphics::LineBlend{shift}};
    const coney::graphics::ScreenSteps steps = coney::graphics::screenStepsOf(pass, false);
    REQUIRE(steps.steps.size() == 2);
    CHECK(copyAt(steps.steps, 0) == ScreenCopyStep{ScreenSlot::View, kView});
    const ScreenDrawStep draw = drawAt(steps.steps, 1);
    CHECK(draw.from == ScreenSlot::View);
    CHECK_FALSE(draw.opaque);
    CHECK(draw.alpha == kQuadTest);
    CHECK(draw.corners[0].colour == Rgba{255, 255, 255, 128});
    checkQuad(draw, 0, 0, 640, 448, 0, shift, 1, 1.0F + shift);
    CHECK_FALSE(steps.forgetKept);

    // An empty view draws nothing.
    CHECK(coney::graphics::screenStepsOf(ScreenPass{ScreenRect{0, 0, 0, 448}, coney::graphics::LineBlend{shift}}, false)
              .steps.empty());
}

TEST_CASE("the motion blur lays the kept frame over the view only while one is kept, then keeps the view",
          "[graphics][screen-passes]") {
    // Expected: src/platform/gl3_screen_passes.cpp, the GL3 renderer's drawMotionBlur() (Coney's stand-in,
    // platform/motion_blur_pass.h): the kept frame over the new one as the view's quad in the blur's colour, blended by
    // its alpha, texture (0, 0) at the view's top left; then the blended view copied into the kept frame for the next
    // frame. With no frame kept yet (the first blur, or the window resized) only the copy.
    const Rgba colour{200, 150, 100, 60};
    const ScreenPass pass{ScreenRect{0, 0, 320, 240}, coney::graphics::MotionBlurOverlay{3, colour}};
    const coney::graphics::ScreenSteps kept = coney::graphics::screenStepsOf(pass, true);
    REQUIRE(kept.steps.size() == 2);
    const ScreenDrawStep draw = drawAt(kept.steps, 0);
    CHECK(draw.from == ScreenSlot::Kept);
    CHECK_FALSE(draw.opaque);
    CHECK(draw.alpha == kQuadTest);
    CHECK(draw.corners[2].colour == colour);
    checkQuad(draw, 0, 0, 320, 240, 0, 0, 1, 1);
    CHECK(copyAt(kept.steps, 1) == ScreenCopyStep{ScreenSlot::Kept, ScreenRect{0, 0, 320, 240}});
    CHECK_FALSE(kept.forgetKept);

    const coney::graphics::ScreenSteps fresh = coney::graphics::screenStepsOf(pass, false);
    REQUIRE(fresh.steps.size() == 1);
    CHECK(copyAt(fresh.steps, 0) == ScreenCopyStep{ScreenSlot::Kept, ScreenRect{0, 0, 320, 240}});
}

TEST_CASE("a motion blur of no strength lays nothing and forgets the kept frame", "[graphics][screen-passes]") {
    // Expected: src/platform/gl3_screen_passes.cpp, the GL3 renderer's drawMotionBlur(): an alpha of 0 draws nothing,
    // copies nothing and marks the kept frame as not kept, so the next blur starts from a fresh frame
    // (graphics::MotionBlurOverlay).
    const ScreenPass pass{kView, coney::graphics::MotionBlurOverlay{3, Rgba{255, 255, 255, 0}}};
    const coney::graphics::ScreenSteps steps = coney::graphics::screenStepsOf(pass, true);
    CHECK(steps.steps.empty());
    CHECK(steps.forgetKept);
}
