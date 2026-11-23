// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/screen_pass_steps.h"

#include <array>
#include <variant>
#include <vector>

#include "graphics/render_device.h"

namespace coney::graphics {

namespace {

// The alpha test the GL3 renderer's screen quads meet: librw's after its start, as every draw that changes it puts it
// back (kStartAlphaRef).
constexpr AlphaRange kQuadTest{kStartAlphaRef, 1000.0F};

// A quad over the window rectangle (`x`, `y`, `width`, `height`) with texture coordinates `uv` in `colour`, its corners
// clockwise from the top left, as the GL3 renderer's drawWindowQuads() makes them.
// @coney-only the GPU renderer's layout of a screen quad; the original draws through its device
std::array<Overlay2dVertex, 4> quadCorners(float x, float y, float width, float height, const UvRect& uv, Rgba colour) {
    return {{{x, y, uv.u0, uv.v0, colour},
             {x + width, y, uv.u1, uv.v0, colour},
             {x + width, y + height, uv.u1, uv.v1, colour},
             {x, y + height, uv.u0, uv.v1, colour}}};
}

// A draw step from `from` over that rectangle.
// @coney-only the GPU renderer's layout of a screen quad; the original draws through its device
ScreenDrawStep quadFrom(ScreenSlot from, bool opaque, float x, float y, float width, float height, const UvRect& uv,
                        Rgba colour) {
    return ScreenDrawStep{from, opaque, quadCorners(x, y, width, height, uv, colour), kQuadTest};
}

// The line blend over `view`: the view copied, then laid over itself at half strength one original line down.
// @coney-only the PS2 video output's softening (docs/research/rendering.md#output), done by its read circuits, not code
std::vector<ScreenStep> lineBlendSteps(const LineBlend& blend, const ScreenRect& view) {
    if (view.width <= 0 || view.height <= 0) {
        return {};
    }
    // PMODE's ALP 0x80 mixes the two read circuits half and half; DISPFB2's DBY 1 starts the second one line down.
    const UvRect uv{0.0F, blend.lineShift, 1.0F, 1.0F + blend.lineShift};
    return {ScreenCopyStep{ScreenSlot::View, view},
            quadFrom(ScreenSlot::View, false, static_cast<float>(view.x), static_cast<float>(view.y),
                     static_cast<float>(view.width), static_cast<float>(view.height), uv, Rgba{255, 255, 255, 128})};
}

// The motion blur over `view` (Coney's stand-in): the kept frame laid over the view while one is kept, then the view
// kept.
// @coney-only the motion blur's stand-in (platform/motion_blur_pass.h), the GL3 renderer's drawMotionBlur() as steps
ScreenSteps motionBlurSteps(const MotionBlurOverlay& blur, const ScreenRect& view, bool keptReady) {
    // No strength: nothing laid, and the next blur starts from a fresh frame.
    if (blur.colour.a == 0) {
        return ScreenSteps{{}, true};
    }
    if (view.width <= 0 || view.height <= 0) {
        return {};
    }
    ScreenSteps steps;
    if (keptReady) {
        steps.steps.emplace_back(quadFrom(ScreenSlot::Kept, false, static_cast<float>(view.x),
                                          static_cast<float>(view.y), static_cast<float>(view.width),
                                          static_cast<float>(view.height), UvRect{}, blur.colour));
    }
    steps.steps.emplace_back(ScreenCopyStep{ScreenSlot::Kept, view});
    return steps;
}

} // namespace

// The steps of the pass's kind.
// @coney-only the GPU renderer's layout of the screen passes; the original draws through its device
ScreenSteps screenStepsOf(const ScreenPass& pass, bool keptReady) {
    if (const auto* blur = std::get_if<BlurPulse>(&pass.effect); blur != nullptr) {
        return ScreenSteps{blurPulseSteps(*blur, pass.area), false};
    }
    if (const auto* lines = std::get_if<LineBlend>(&pass.effect); lines != nullptr) {
        return ScreenSteps{lineBlendSteps(*lines, pass.area), false};
    }
    return motionBlurSteps(std::get<MotionBlurOverlay>(pass.effect), pass.area, keptReady);
}

std::vector<ScreenStep> blurPulseSteps(const BlurPulse& blur, const ScreenRect& view) {
    // The whole picture the 3D view fills, as the original blurs its whole screen.
    if (view.width < 2 || view.height < 2) {
        return {};
    }
    const auto left = static_cast<float>(view.x);
    const auto top = static_cast<float>(view.y);
    const int halfWidth = view.width / 2;
    const int halfHeight = view.height / 2;
    const ScreenRect halfRect{view.x, view.y, halfWidth, halfHeight};
    const Rgba white{255, 255, 255, 255};
    std::vector<ScreenStep> steps;

    // 1. The view at half size into its own top-left quarter. The original draws it into its 512 x 256 raster through
    // the effects camera (UVs offset by half a texel, 0x0050ce58); Coney draws into the frame and copies the result
    // back before the next step reads it, with UVs spanning each copy (stand-in, as the GL3 renderer's).
    steps.emplace_back(ScreenCopyStep{ScreenSlot::View, view});
    const ScreenDrawStep half = quadFrom(ScreenSlot::View, true, left, top, static_cast<float>(halfWidth),
                                         static_cast<float>(halfHeight), UvRect{}, white);
    steps.emplace_back(half);

    // 2. The passes: the half-size image onto itself with one edge moved in by half the offset, the low edges for the
    // offset as given and the high ones once its sign has flipped (0x0050ce64), flipping each pass. The offsets are
    // UV units of the original's 512 x 256 raster, whose half-size image is 320 x 224 (inferred: a 640 x 448 screen at
    // half size), so they are scaled to the same share of the copy here (stand-in, as the GL3 renderer's).
    const float stepU = blur.offsetU / 2.0F * (512.0F / 320.0F);
    const float stepV = blur.offsetV / 2.0F * (256.0F / 224.0F);
    bool lowEdge = true;
    for (int pass = 0; pass < blur.passes; ++pass) {
        // Stand-in: a copy of the half-size image for the pass to read, where the original draws its raster onto
        // itself through the effects camera.
        steps.emplace_back(ScreenCopyStep{ScreenSlot::Half, halfRect});
        ScreenDrawStep moved = half;
        moved.from = ScreenSlot::Half;
        UvRect uv;
        if (lowEdge) {
            uv.u0 += stepU;
            uv.v0 += stepV;
        } else {
            uv.u1 -= stepU;
            uv.v1 -= stepV;
        }
        moved.corners =
            quadCorners(left, top, static_cast<float>(halfWidth), static_cast<float>(halfHeight), uv, white);
        steps.emplace_back(moved);
        // The sign flipped for the next pass. Stand-in: the depth and blending states the original puts back after
        // each step are not, as every step is drawn in its own.
        lowEdge = !lowEdge;
    }

    // 3. Stretched back over the whole view through the view's camera, its edges inset by one texel (0x0050ce60 with
    // the flag 0x0050ce68), opaque.
    steps.emplace_back(ScreenCopyStep{ScreenSlot::Half, halfRect});
    const float texelU = 1.0F / static_cast<float>(halfWidth);
    const float texelV = 1.0F / static_cast<float>(halfHeight);
    steps.emplace_back(quadFrom(ScreenSlot::Half, true, left, top, static_cast<float>(view.width),
                                static_cast<float>(view.height), UvRect{texelU, texelV, 1.0F - texelU, 1.0F - texelV},
                                white));
    // Stand-in: the original then puts the depth test and write and the blending by alpha back, ends the camera's
    // update and lets its raster go; the steps leave nothing set, as the renderer draws each in its own states.
    return steps;
}

} // namespace coney::graphics
