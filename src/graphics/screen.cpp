// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/screen.h"

#include <cstdint>

namespace coney::graphics {

ScreenRect fitLogicalScreen(Extent window, DisplayAspect aspect) {
    if (window.width <= 0 || window.height <= 0 || aspect.width <= 0 || aspect.height <= 0) {
        return ScreenRect{};
    }
    // Compare the two limits without division: the width limits when window.width / aspect.width is the smaller.
    const std::int64_t widthLimited = std::int64_t{window.width} * aspect.height;
    const std::int64_t heightLimited = std::int64_t{window.height} * aspect.width;
    int width = window.width;
    int height = window.height;
    if (widthLimited <= heightLimited) {
        height = static_cast<int>(widthLimited / aspect.width);
    } else {
        width = static_cast<int>(heightLimited / aspect.height);
    }
    return ScreenRect{(window.width - width) / 2, (window.height - height) / 2, width, height};
}

LogicalRect logicalToWindow(const LogicalRect& rect, const ScreenRect& viewport) {
    const float scaleX = static_cast<float>(viewport.width) / kLogicalWidth;
    const float scaleY = static_cast<float>(viewport.height) / kLogicalHeight;
    return LogicalRect{static_cast<float>(viewport.x) + rect.x * scaleX,
                       static_cast<float>(viewport.y) + rect.y * scaleY, rect.width * scaleX, rect.height * scaleY};
}

float originalLineShare(const ScreenRect& logicalScreen, const ScreenRect& view) {
    if (view.height <= 0) {
        return 0.0F;
    }
    // One line is the logical screen's height over 448, in window pixels; as a share of the view's height.
    return static_cast<float>(logicalScreen.height) / kLogicalHeight / static_cast<float>(view.height);
}

} // namespace coney::graphics
