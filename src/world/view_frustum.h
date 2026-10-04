// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "world/world_streams.h"

namespace coney::world {

/// Where a camera is and where it looks, in RenderWare's axes (y up in the streamed worlds). The three directions are
/// unit length and at right angles; `right` is the direction that appears to the right on the screen.
struct CameraPose {
    Vec3 position;
    Vec3 forward{0.0F, 0.0F, 1.0F};
    Vec3 up{0.0F, 1.0F, 0.0F};
    Vec3 right{-1.0F, 0.0F, 0.0F};
};

/// The part of space a perspective camera sees: the volume between the near and far clip planes inside its view
/// window (RenderWare's: half the window's width and height at distance 1 in front of the camera).
///
/// It stands for the frustum test RenderWare makes while it walks a world's BSP for the visibility pass
/// (docs/research/world.md#visibility); Coney has no BSP walk, so each sector box is tested on its own.
class ViewFrustum {
  public:
    /// The frustum of a camera at `pose` with a view window of `halfWidth` × `halfHeight` and the two clip distances.
    ViewFrustum(const CameraPose& pose, float halfWidth, float halfHeight, float nearClip, float farClip);

    /// Whether any part of `box` may be inside. Conservative: a box near a corner of the frustum can pass while lying
    /// just outside, as with any plane-by-plane test; it never rejects a box that is inside.
    [[nodiscard]] bool mayContain(const Box& box) const;

  private:
    // A plane `normal · p + offset >= 0` on the inside.
    struct Plane {
        Vec3 normal;
        float offset = 0.0F;
    };
    std::array<Plane, 6> m_planes{};
};

} // namespace coney::world
