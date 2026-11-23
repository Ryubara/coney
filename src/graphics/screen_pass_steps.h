// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

#include "graphics/frame_description.h"
#include "graphics/overlay_batch.h"
#include "graphics/texture_grid.h"

namespace coney::graphics {

/// A copy of the frame a screen pass reads: the view's (the line blend's and the blur pulse's first), the blur pulse's
/// half-size image, or the motion blur's kept frame, which lasts from one frame to the next (MotionBlurOverlay::copy).
enum class ScreenSlot : std::uint8_t { View, Half, Kept };

/// A step that copies the rectangle `from` of what is drawn so far (window pixels from the top left) into the copy
/// `into`, made at that size; texel (0, 0) of the copy is the rectangle's top-left pixel.
struct ScreenCopyStep {
    ScreenSlot into = ScreenSlot::View;
    ScreenRect from;

    friend bool operator==(const ScreenCopyStep&, const ScreenCopyStep&) = default;
};

/// A step that draws one quad from the copy `from` in the 2D pass's states (no depth, no culling, no fog), its corners
/// clockwise from the top left as the GL3 renderer's screen quads, sampled linearly and clamped: `opaque` replaces what
/// is below (ONE / ZERO), otherwise it is blended by source alpha. `alpha` is the alpha test it is drawn with.
struct ScreenDrawStep {
    ScreenSlot from = ScreenSlot::View;
    bool opaque = false;
    std::array<Overlay2dVertex, 4> corners{};
    AlphaRange alpha;

    friend bool operator==(const ScreenDrawStep&, const ScreenDrawStep&) = default;
};

/// One step of a screen pass.
using ScreenStep = std::variant<ScreenCopyStep, ScreenDrawStep>;

/// A screen pass as copies and draws in order, the GPU renderer's way of drawing it (platform/gpu_renderer.h), which
/// cannot read the frame it draws into: each draw reads only copies made before it.
struct ScreenSteps {
    std::vector<ScreenStep> steps;
    /// The motion blur's kept frame is forgotten (a blur of no strength), so the next blur starts afresh.
    bool forgetKept = false;

    friend bool operator==(const ScreenSteps&, const ScreenSteps&) = default;
};

/// The steps of `pass`, drawn as the GL3 renderer draws it (platform/gl3_screen_passes.cpp), each draw with the alpha
/// test the GL3 renderer's screen quads meet (librw's after its start, kStartAlphaRef):
///
/// - the blur pulse (blurPulseSteps());
/// - the line blend: the view copied, then laid over itself at half strength (alpha 128), shifted down by its
///   `lineShift` of the view's height;
/// - the motion blur: while `keptReady` (a frame of the view's size is kept for its copy), the kept frame laid over
///   the view in its colour, blended by the colour's alpha; then the view copied into the kept frame. A colour of alpha
///   0 has no steps and forgets the kept frame.
///
/// An empty view has no steps.
/// @coney-only the GPU renderer's layout of the screen passes (docs/engine/gpu-renderer.md#screen-passes); the original
/// draws through its device
[[nodiscard]] ScreenSteps screenStepsOf(const ScreenPass& pass, bool keptReady);

/// The blur pulse's steps over `view`, the blur pass of the original's device (device slot `+0x108`,
/// docs/research/graphics.md#blur-pulse): the view copied and drawn at half size into its own top-left quarter; then
/// `blur.passes` times the half-size image copied and drawn onto itself with one edge moved in by half the offset, the
/// low edges and the high edges in turn; then copied once more and stretched back over the whole view, its edges inset
/// by one texel, opaque. Every draw is opaque. A view under 2 pixels either way has no steps.
/// @orig 0x00193d80 RwDevice_BlurPass (unknown)
[[nodiscard]] std::vector<ScreenStep> blurPulseSteps(const BlurPulse& blur, const ScreenRect& view);

} // namespace coney::graphics
