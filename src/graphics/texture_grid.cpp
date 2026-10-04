// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/texture_grid.h"

#include <algorithm>
#include <cstddef>

namespace coney::graphics {

namespace {

// The size of one cell when `count` cells of a grid share `length` pixels with `margin` around and between them;
// never negative.
int cellLength(int length, int count, int margin) { return std::max(0, (length - (count + 1) * margin) / count); }

// The rectangle of a `texture` scaled to fit a `cellWidth` x `cellHeight` cell whose top-left corner is (x, y),
// keeping its shape and centring it.
ScreenRect fitInCell(Extent texture, int x, int y, int cellWidth, int cellHeight) {
    if (texture.width <= 0 || texture.height <= 0) {
        return ScreenRect{x + cellWidth / 2, y + cellHeight / 2, 0, 0};
    }
    // Compare the two scale factors without division: width-limited when cellWidth/w <= cellHeight/h.
    const long long widthLimited = static_cast<long long>(cellWidth) * texture.height;
    const long long heightLimited = static_cast<long long>(cellHeight) * texture.width;
    int width = cellWidth;
    int height = cellHeight;
    if (widthLimited <= heightLimited) {
        height = static_cast<int>(widthLimited / texture.width);
    } else {
        width = static_cast<int>(heightLimited / texture.height);
    }
    return ScreenRect{x + (cellWidth - width) / 2, y + (cellHeight - height) / 2, width, height};
}

} // namespace

GridLayout layoutGrid(std::span<const Extent> textures, Extent screen, int margin) {
    GridLayout layout;
    const auto count = static_cast<int>(textures.size());
    if (count == 0) {
        return layout;
    }

    // Try every column count and keep the one whose cells' shorter side is longest; ties keep fewer columns.
    int bestSide = -1;
    for (int columns = 1; columns <= count; ++columns) {
        const int rows = (count + columns - 1) / columns;
        const int side = std::min(cellLength(screen.width, columns, margin), cellLength(screen.height, rows, margin));
        if (side > bestSide) {
            bestSide = side;
            layout.columns = columns;
            layout.rows = rows;
        }
    }

    // Place each texture in its cell, row by row from the top left.
    const int cellWidth = cellLength(screen.width, layout.columns, margin);
    const int cellHeight = cellLength(screen.height, layout.rows, margin);
    layout.quads.reserve(textures.size());
    for (int i = 0; i < count; ++i) {
        const int column = i % layout.columns;
        const int row = i / layout.columns;
        const int x = margin + column * (cellWidth + margin);
        const int y = margin + row * (cellHeight + margin);
        layout.quads.push_back(fitInCell(textures[static_cast<std::size_t>(i)], x, y, cellWidth, cellHeight));
    }
    return layout;
}

} // namespace coney::graphics
