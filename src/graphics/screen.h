// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "graphics/texture_grid.h"

namespace coney::graphics {

/// Width of the original's screen in pixels: the 640 x 448 interlaced video mode its device chooses
/// (docs/research/graphics.md#video-mode). Every 2D position Coney draws is in these logical pixels.
inline constexpr float kLogicalWidth = 640.0F;
/// Height of the original's screen in pixels (448; 480 in its progressive mode, which Coney does not have).
inline constexpr float kLogicalHeight = 448.0F;

/// The shape a television gives the logical screen: 4:3, or 16:9 with the original's widescreen option. The
/// 640 x 448 pixels are not square; the overlay camera's 4:3 view window (docs/research/graphics.md#2d-drawing) is
/// what makes a circle round on the television, so Coney shows the logical screen at this shape.
struct DisplayAspect {
    int width = 4;
    int height = 3;
};

/// The 4:3 display of the original's default mode.
inline constexpr DisplayAspect kStandardAspect{4, 3};

/// Where the logical screen goes in a window of `window` pixels: the largest rectangle of `aspect`'s shape that fits,
/// centred, in whole pixels. The rest of the window is a black border (letterboxing or pillarboxing).
///
/// Coney's choice: the original fills a television picture, which has no border; a window of another shape gets
/// black bars rather than a stretched or cropped picture. A window of size 0 gives an empty rectangle.
[[nodiscard]] ScreenRect fitLogicalScreen(Extent window, DisplayAspect aspect = kStandardAspect);

/// The 3D view's width over its height on the original's 4:3 display. Coney's 3D view keeps the window's shape
/// (platform::RenderEngine::viewAspect()); a view wider than this is the original's 16:9 mode (isWideView()).
inline constexpr float kStandardViewAspect = 4.0F / 3.0F;

/// Whether a 3D view of `viewAspect` (width over height) is shown as the original's 16:9 mode: any view wider than
/// 4:3, with a little slack for a window rounded to whole pixels. **Coney's reading**: the original has a 16:9 option
/// (device flag `0x04`, docs/research/graphics.md#video-mode); Coney has no such setting and takes the window's shape
/// instead, keeping the view's height and widening it as that mode does.
[[nodiscard]] constexpr bool isWideView(float viewAspect) { return viewAspect > kStandardViewAspect + 0.01F; }

/// The height of one of the original's 448 lines as a share of `view`'s height, where the logical screen is
/// `logicalScreen` in the same window: for passes that shift the whole view by one original line (the video output's
/// line blend, docs/research/rendering.md#output). 1 / 448 when the logical screen is as tall as the view (a 4:3 or a
/// wider window); 0 for an empty view.
[[nodiscard]] float originalLineShare(const ScreenRect& logicalScreen, const ScreenRect& view);

/// A rectangle of the logical screen, in logical pixels from the top-left corner (floats: the window scales them).
struct LogicalRect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    friend bool operator==(const LogicalRect&, const LogicalRect&) = default;
};

/// Maps `rect` in logical pixels into window pixels, given where fitLogicalScreen() put the logical screen.
[[nodiscard]] LogicalRect logicalToWindow(const LogicalRect& rect, const ScreenRect& viewport);

} // namespace coney::graphics
