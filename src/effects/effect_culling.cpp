// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/effect_culling.h"

#include <algorithm>
#include <cmath>

#include "camera/camera_lens.h"

namespace coney::effects {

bool pointInView(const camera::CameraView& view, anim::Vec3 point, float margin) {
    // The point in the camera's frame: across (right), along the view, and up.
    const anim::Vec3 forward = camera::viewForward(view);
    const anim::Vec3 up = camera::viewUp(view);
    const anim::Vec3 right = anim::cross(forward, up);
    const anim::Vec3 offset = anim::subtract(point, view.position);
    const float x = anim::dot(offset, right);
    const float y = anim::dot(offset, forward);
    const float z = anim::dot(offset, up);
    const camera::ViewWindow window =
        camera::viewWindow(camera::CameraLens{view.fieldOfView, view.nearClip, view.farClip});
    // How far outside each plane the point lies (negative inside): near, far, then the four sides, each side's
    // distance divided by its normal's length.
    const float sideX = std::sqrt(1.0F + (window.halfWidth * window.halfWidth));
    const float sideZ = std::sqrt(1.0F + (window.halfHeight * window.halfHeight));
    const float outside = std::max({view.nearClip - y, y - view.farClip, (x - (y * window.halfWidth)) / sideX,
                                    (-x - (y * window.halfWidth)) / sideX, (z - (y * window.halfHeight)) / sideZ,
                                    (-z - (y * window.halfHeight)) / sideZ});
    return outside <= margin;
}

bool effectNearView(const camera::CameraView& view, anim::Vec3 point, float range, float margin) {
    return anim::distance(point, view.position) <= range && pointInView(view, point, margin);
}

} // namespace coney::effects
