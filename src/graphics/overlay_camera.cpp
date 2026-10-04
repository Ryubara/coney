// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/overlay_camera.h"

#include "core/assert.h"

namespace coney::graphics {

OverlayPoint OverlayCamera::guiToOverlay(float x, float y) {
    return OverlayPoint{(x - 0.5F) * kLogicalWidth / kLogicalHeight, 0.5F - y, kGuiDepth};
}

float OverlayCamera::guiWidthToOverlay(float width) { return width * kLogicalWidth / kLogicalHeight; }

LogicalPoint OverlayCamera::project(const OverlayPoint& point) const {
    CONEY_ASSERT(point.z > 0.0F);
    // At depth z the camera sees x in [-viewWindowX * z, viewWindowX * z] across the screen's width, y likewise up
    // the height (RenderWare's view window is half the visible extent at distance 1).
    const float across = 0.5F + point.x / (2.0F * viewWindowX() * point.z);
    const float down = 0.5F - point.y / (2.0F * viewWindowY() * point.z);
    return LogicalPoint{across * kLogicalWidth, down * kLogicalHeight};
}

LogicalPoint OverlayCamera::projectSize(float width, float height, float z) const {
    CONEY_ASSERT(z > 0.0F);
    return LogicalPoint{width / (2.0F * viewWindowX() * z) * kLogicalWidth,
                        height / (2.0F * viewWindowY() * z) * kLogicalHeight};
}

OverlayPoint OverlayCamera::unproject(const LogicalPoint& point, float z) const {
    CONEY_ASSERT(z > 0.0F);
    const float across = point.x / kLogicalWidth;
    const float down = point.y / kLogicalHeight;
    return OverlayPoint{(across - 0.5F) * 2.0F * viewWindowX() * z, (0.5F - down) * 2.0F * viewWindowY() * z, z};
}

} // namespace coney::graphics
