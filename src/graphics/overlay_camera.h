// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "graphics/screen.h"

namespace coney::graphics {

/// A point in the overlay camera's space: x to the right, y up, z the distance in front of the camera.
struct OverlayPoint {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend bool operator==(const OverlayPoint&, const OverlayPoint&) = default;
};

/// A point of the logical screen, in logical pixels from the top-left corner.
struct LogicalPoint {
    float x = 0.0F;
    float y = 0.0F;

    friend bool operator==(const LogicalPoint&, const LogicalPoint&) = default;
};

/// The overlay camera, which sees the HUD and the front end, and the GUI coordinates that are placed in it.
///
/// GUI coordinates: x to the right and y down, the whole screen about [0, 1]². The device turns a GUI point into the
/// overlay camera's space at a fixed depth, and the camera's perspective projection puts it on the screen. Because the
/// camera's view window is a little larger than the GUI square at that depth, GUI 0 and 1 land about 1.3 % and 98.7 %
/// across the width and 4.5 % and 95.5 % down the height: a margin built into the projection, not into the menus.
///
/// Coney does not render through a librw camera for this; it computes the same projection and draws in logical
/// pixels (graphics/screen.h).
///
/// Research: docs/research/graphics.md#2d-drawing, docs/research/graphics.md#a-frame
class OverlayCamera {
  public:
    /// The depth at which GUI points are placed, in front of the camera.
    static constexpr float kGuiDepth = 1.1F;
    /// The aspect of the overlay camera's view window (`0x0050b208`): 4:3.
    static constexpr float kViewAspect = 4.0F / 3.0F;

    /// The camera of the 4:3 screen, view-window scale 1.0. The original's 16:9 option uses a scale of 1.1
    /// (`0x0050b20c`), which Coney does not have yet.
    explicit OverlayCamera(float viewScale = 1.0F) : m_viewScale(viewScale) {}

    /// Half the width of the view window at distance 1: scale × aspect × 0.5 = 0.6667 at scale 1.0.
    [[nodiscard]] float viewWindowX() const { return m_viewScale * kViewAspect * 0.5F; }
    /// Half the height of the view window at distance 1: scale × 0.5.
    [[nodiscard]] float viewWindowY() const { return m_viewScale * 0.5F; }

    /// A GUI point in the overlay camera's space: ((x - 0.5) × W / H, 0.5 - y, kGuiDepth), W and H the screen's 640 and
    /// 448.
    /// @orig 0x00195238 RwDevice::GuiToOverlay (DevRWGeneric.cpp)
    [[nodiscard]] static OverlayPoint guiToOverlay(float x, float y);

    /// A GUI width in the overlay camera's space: width × W / H. GUI heights need no conversion.
    /// @orig 0x00195330 RwDevice::GuiWidthToOverlay (DevRWGeneric.cpp)
    [[nodiscard]] static float guiWidthToOverlay(float width);

    /// Where a point of the overlay camera's space lands on the logical screen, by the camera's perspective
    /// projection. `point.z` must be positive (checked by CONEY_ASSERT).
    [[nodiscard]] LogicalPoint project(const OverlayPoint& point) const;

    /// The logical size of something `width` × `height` large in the overlay camera's space at depth `z`, which must
    /// be positive (checked by CONEY_ASSERT).
    [[nodiscard]] LogicalPoint projectSize(float width, float height, float z) const;

    /// The overlay-space point at depth `z` that projects onto the logical point `point`: the inverse of project(),
    /// for Coney's tools that lay things out in pixels. `z` must be positive (checked by CONEY_ASSERT).
    [[nodiscard]] OverlayPoint unproject(const LogicalPoint& point, float z) const;

    /// The overlay-space width and height at depth `z` that project to a logical size of `size`: the inverse of
    /// projectSize(). `z` must be positive (checked by CONEY_ASSERT).
    [[nodiscard]] LogicalPoint unprojectSize(const LogicalPoint& size, float z) const;

    /// A GUI point straight onto the logical screen: guiToOverlay(), then project().
    [[nodiscard]] LogicalPoint guiToLogical(float x, float y) const { return project(guiToOverlay(x, y)); }

  private:
    float m_viewScale;
};

} // namespace coney::graphics
