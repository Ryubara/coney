// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

namespace coney::graphics {

/// A size in pixels.
struct Extent {
    int width = 0;
    int height = 0;

    friend bool operator==(const Extent&, const Extent&) = default;
};

/// A rectangle in screen pixels, from the top-left corner.
struct ScreenRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const ScreenRect&, const ScreenRect&) = default;
};

/// Where a grid of textures goes on the screen: one rectangle per texture, in the order given.
struct GridLayout {
    int columns = 0;
    int rows = 0;
    std::vector<ScreenRect> quads;
};

/// Lays out `textures` in a grid of equal cells filling `screen`, with `margin` pixels around and between the cells.
/// The column count is the one that gives the largest cells. Each texture is scaled by the same factor in both
/// directions to fill its cell as far as its shape allows (up or down) and is centred in it; positions and sizes are
/// whole pixels, so texels stay sharp. Cells fill row by row from the top left.
///
/// No textures gives an empty layout. A screen too small for the cells gives rectangles of size 0, never negative.
/// Coney's own tool code (the texture viewer); nothing in the original corresponds.
[[nodiscard]] GridLayout layoutGrid(std::span<const Extent> textures, Extent screen, int margin);

} // namespace coney::graphics
