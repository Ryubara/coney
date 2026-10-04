// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/view_frustum.h"

namespace coney::world {

namespace {

// a * s + b * t, componentwise.
Vec3 combine(Vec3 a, float s, Vec3 b, float t) { return Vec3{a.x * s + b.x * t, a.y * s + b.y * t, a.z * s + b.z * t}; }

// The dot product.
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

} // namespace

ViewFrustum::ViewFrustum(const CameraPose& pose, float halfWidth, float halfHeight, float nearClip, float farClip) {
    // Each plane through the camera (or at a clip distance in front of it), its normal pointing inwards. A point d in
    // front of the camera is inside the side planes when |right · d| <= halfWidth × (forward · d), and likewise up.
    const auto through = [&pose](Vec3 normal, float distance) {
        return Plane{normal, -dot(normal, pose.position) - distance};
    };
    m_planes[0] = through(pose.forward, nearClip);
    m_planes[1] = through(combine(pose.forward, -1.0F, pose.forward, 0.0F), -farClip);
    m_planes[2] = through(combine(pose.forward, halfWidth, pose.right, -1.0F), 0.0F);
    m_planes[3] = through(combine(pose.forward, halfWidth, pose.right, 1.0F), 0.0F);
    m_planes[4] = through(combine(pose.forward, halfHeight, pose.up, -1.0F), 0.0F);
    m_planes[5] = through(combine(pose.forward, halfHeight, pose.up, 1.0F), 0.0F);
}

bool ViewFrustum::mayContain(const Box& box) const {
    for (const Plane& plane : m_planes) {
        // The corner farthest along the normal: if even that is outside, the whole box is.
        const Vec3 corner{plane.normal.x >= 0.0F ? box.max.x : box.min.x,
                          plane.normal.y >= 0.0F ? box.max.y : box.min.y,
                          plane.normal.z >= 0.0F ? box.max.z : box.min.z};
        if (dot(plane.normal, corner) + plane.offset < 0.0F) {
            return false;
        }
    }
    return true;
}

} // namespace coney::world
