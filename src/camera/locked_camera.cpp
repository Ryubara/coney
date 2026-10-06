// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/locked_camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::camera {

namespace {

constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;

} // namespace

CameraView LockedCamera::view() const {
    // The forward of the heading (0 facing +y, anticlockwise) tipped down by the pitch.
    const float heading = headingDegrees * kRadians;
    const float pitch = pitchDegrees * kRadians;
    const anim::Vec3 forward{-std::sin(heading) * std::cos(pitch), std::cos(heading) * std::cos(pitch),
                             -std::sin(pitch)};
    return CameraView{.position = position,
                      .orientation = lookRotation(forward, rollDegrees * kRadians),
                      .lookAt = anim::add(position, anim::scale(forward, kAimDistance)),
                      .fieldOfView = fieldOfView,
                      .nearClip = nearClip,
                      .farClip = std::min(farClip, kMaxFarClip)};
}

std::array<ViewSide, 2> viewSides(const CameraView& view) {
    const anim::Vec3 forward = viewForward(view);
    const anim::Vec3 right = anim::normalise(anim::cross(forward, viewUp(view)));
    const float half = view.fieldOfView * 0.5F * kRadians;
    // A side's inward normal leans from across the view toward the view direction.
    const anim::Vec3 left = anim::add(anim::scale(right, std::cos(half)), anim::scale(forward, std::sin(half)));
    const anim::Vec3 rightSide = anim::add(anim::scale(right, -std::cos(half)), anim::scale(forward, std::sin(half)));
    return {ViewSide{.normal = left, .w = anim::dot(left, view.position)},
            ViewSide{.normal = rightSide, .w = anim::dot(rightSide, view.position)}};
}

std::optional<anim::Vec3> keepInView(const CameraView& view, anim::Vec3 feet, anim::Vec3 before,
                                     const raycast::CollisionMesh* mesh, bool alreadyPushed) {
    using Rules = KeepInViewRules;
    const anim::Vec3 up{0.0F, 0.0F, Rules::kHeadHeight};
    anim::Vec3 p = anim::add(feet, up);
    const anim::Vec3 q = anim::add(before, up);
    // The left side first; the right only when the left did not push.
    std::optional<ViewSide> pushedBy;
    for (const ViewSide& side : viewSides(view)) {
        const float inside = anim::dot(p, side.normal) - side.w;
        if (inside < Rules::kMargin) {
            p = anim::add(p, anim::scale(side.normal, Rules::kMargin - inside + Rules::kNudge));
            pushedBy = side;
            break;
        }
    }
    if (!pushedBy && !alreadyPushed) {
        return std::nullopt;
    }
    // The move's part along the side, clipped by the world from where the human was. **Coney's reading** of "the part
    // of the step along the plane": the move less its part along the side's normal.
    if (pushedBy && mesh != nullptr) {
        const anim::Vec3 step = anim::subtract(p, q);
        const anim::Vec3 across = anim::scale(pushedBy->normal, anim::dot(step, pushedBy->normal));
        const anim::Vec3 along = anim::subtract(step, across);
        const float length = anim::length(along);
        if (length > 0.0F) {
            const anim::Vec3 direction = anim::scale(along, 1.0F / length);
            const raycast::Ray ray{.origin = {q.x, q.y, q.z},
                                   .direction = {direction.x, direction.y, direction.z},
                                   .length = length + Rules::kMargin};
            if (const std::optional<raycast::RayHit> hit = mesh->rayCast(ray, {}, 0)) {
                const float kept = std::clamp(hit->t - Rules::kMargin, 0.0F, length);
                p = anim::add(anim::add(q, across), anim::scale(direction, kept));
            }
        }
    }
    // Back down to the feet, snapped to the ground below.
    anim::Vec3 placed = anim::subtract(p, up);
    if (mesh != nullptr) {
        const raycast::Ray down{.origin = {placed.x, placed.y, placed.z + Rules::kGroundFrom},
                                .direction = {0.0F, 0.0F, -1.0F},
                                .length = Rules::kGroundRay};
        if (const std::optional<raycast::RayHit> hit = mesh->rayCast(down, {}, 0)) {
            placed.z = placed.z + Rules::kGroundFrom - hit->t + Rules::kAboveGround;
        }
    }
    return placed;
}

} // namespace coney::camera
