// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/camera_view.h"

#include <cmath>

namespace coney::camera {

anim::Quat lookRotation(anim::Vec3 forward, float roll) {
    const anim::Vec3 y = anim::normalise(forward);
    // Right is forward × up; straight up or down there is no such right, so +y stands in for up.
    anim::Vec3 x = anim::cross(y, anim::Vec3{0.0F, 0.0F, 1.0F});
    if (anim::length(x) < 1e-6F) {
        x = anim::cross(y, anim::Vec3{0.0F, 1.0F, 0.0F});
    }
    x = anim::normalise(x);
    anim::Vec3 z = anim::cross(x, y);
    // The roll turns right and up about the view direction.
    if (roll != 0.0F) {
        const float c = std::cos(roll);
        const float s = std::sin(roll);
        const anim::Vec3 rolledX = anim::add(anim::scale(x, c), anim::scale(z, -s));
        const anim::Vec3 rolledZ = anim::add(anim::scale(x, s), anim::scale(z, c));
        x = rolledX;
        z = rolledZ;
    }
    anim::Mat34 frame;
    frame.x = x;
    frame.y = y;
    frame.z = z;
    return anim::quatFromMatrix(frame);
}

anim::Vec3 viewForward(const CameraView& view) {
    return anim::transformDirection(anim::matrixFromQuat(view.orientation), anim::Vec3{0.0F, 1.0F, 0.0F});
}

anim::Vec3 viewUp(const CameraView& view) {
    return anim::transformDirection(anim::matrixFromQuat(view.orientation), anim::Vec3{0.0F, 0.0F, 1.0F});
}

CameraView viewLookingAt(anim::Vec3 position, anim::Vec3 lookAt, float fieldOfView, float nearClip, float farClip) {
    const anim::Vec3 forward = anim::subtract(lookAt, position);
    return CameraView{.position = position,
                      .orientation = anim::length(forward) > 1e-6F ? lookRotation(forward) : anim::Quat{},
                      .lookAt = lookAt,
                      .fieldOfView = fieldOfView,
                      .nearClip = nearClip,
                      .farClip = farClip};
}

} // namespace coney::camera
