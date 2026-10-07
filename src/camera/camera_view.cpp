// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/camera_view.h"

#include <algorithm>
#include <cmath>

#include "camera/camera_lens.h"
#include "raycast/collision_mesh.h"

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

bool canSeePoint(const CameraView& view, anim::Vec3 point, float range, const raycast::CollisionMesh* mesh) {
    const float reach = range > 0.0F ? std::min(range, view.farClip) : view.farClip;
    const anim::Vec3 to = anim::subtract(point, view.position);
    const float distance = anim::length(to);
    if (distance > reach) {
        return false;
    }
    // In the camera's frame: depth along +y, across along +x, up along +z, against the view window.
    const anim::Mat34 frame = anim::matrixFromQuat(view.orientation);
    const float depth = anim::dot(to, frame.y);
    if (depth < view.nearClip) {
        return false;
    }
    const ViewWindow window =
        viewWindow(CameraLens{.fieldOfView = view.fieldOfView, .nearClip = view.nearClip, .farClip = view.farClip});
    if (std::abs(anim::dot(to, frame.x)) > depth * window.halfWidth ||
        std::abs(anim::dot(to, frame.z)) > depth * window.halfHeight) {
        return false;
    }
    // Nothing of the world in the way.
    if (mesh == nullptr || distance <= 0.0F) {
        return true;
    }
    const anim::Vec3 direction = anim::scale(to, 1.0F / distance);
    const raycast::Ray ray{.origin = {view.position.x, view.position.y, view.position.z},
                           .direction = {direction.x, direction.y, direction.z},
                           .length = distance};
    return !mesh->rayCast(ray, {}, 0).has_value();
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
