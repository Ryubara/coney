// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/follow_collision.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::camera {

namespace {

constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
// The probe angle at the band's near and far edges, degrees.
constexpr float kProbeNear = 7.0F;
constexpr float kProbeSpan = 3.0F;
// The side probes go out to this many probe angles.
constexpr int kProbeSteps = 3;

raycast::Vec3 toMesh(anim::Vec3 v) { return raycast::Vec3{v.x, v.y, v.z}; }

// `v` turned about the vertical by `angle` radians, anticlockwise seen from above.
anim::Vec3 turned(anim::Vec3 v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return anim::Vec3{v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

// Whether a triangle with these flags is switched off (flag bit 0 clear) and not one testable while disabled.
bool disabled(std::uint16_t flags) {
    return (flags & raycast::kTriangleEnabled) == 0 && (flags & raycast::kTriangleTestableDisabled) == 0;
}

} // namespace

float probeAngle(float distance, float bandNear, float bandFar) {
    const float span = bandFar - bandNear;
    const float t = span > 0.0F ? std::clamp((distance - bandNear) / span, 0.0F, 1.0F) : 0.0F;
    return (kProbeNear - kProbeSpan * t) * kRadians;
}

std::optional<raycast::RayHit> castViewRay(const raycast::CollisionMesh& mesh, anim::Vec3 from, anim::Vec3 direction,
                                           float length, anim::Vec3 targetPoint) {
    const raycast::Ray ray{.origin = toMesh(from), .direction = toMesh(direction), .length = length};
    std::optional<raycast::RayHit> hit = mesh.rayCast(ray, kSeeThroughMaterials, kViewRayMask);
    if (!hit || !disabled(hit->flags)) {
        return hit;
    }
    // A disabled triangle stands only while the target is in front of it and the look-at point well clear of it. The
    // hit's normal faces the ray, so `from` is in front of the plane by t × the cosine.
    const anim::Vec3 normal{hit->normal.x, hit->normal.y, hit->normal.z};
    const anim::Vec3 point = anim::add(from, anim::scale(direction, hit->t));
    const float fromFront = anim::dot(normal, anim::subtract(from, point));
    const float targetFront = anim::dot(normal, anim::subtract(targetPoint, point));
    if (targetFront <= 0.0F || fromFront < kRecastNearPlane) {
        return mesh.rayCast(ray, kSeeThroughMaterials, kViewRecastMask);
    }
    return hit;
}

std::array<float, 2> sideRoom(const raycast::CollisionMesh& mesh, anim::Vec3 from, anim::Vec3 direction, float length,
                              float angle, anim::Vec3 targetPoint) {
    std::array<float, 2> room{0.0F, 0.0F};
    for (std::size_t side = 0; side < room.size(); ++side) {
        const float sign = side == 0 ? 1.0F : -1.0F;
        // Out from the view one probe angle at a time, until a probe meets the world.
        for (int step = 1; step <= kProbeSteps; ++step) {
            const float probe = sign * static_cast<float>(step) * angle;
            if (castViewRay(mesh, from, turned(direction, probe), length, targetPoint)) {
                break;
            }
            room.at(side) = static_cast<float>(step) * angle;
        }
    }
    return room;
}

} // namespace coney::camera
