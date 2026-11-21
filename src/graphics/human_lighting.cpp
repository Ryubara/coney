// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/human_lighting.h"

#include <algorithm>
#include <cmath>

namespace coney::graphics {

namespace {

// The ground ray: from this far above the feet, this far down.
constexpr float kRayAbove = 0.25F;
constexpr float kRayLength = 4.0F;
// How far above the ground the shadow lies.
constexpr float kShadowLift = 0.05F;

// The nearest ground below `feet` within the ray, if any.
std::optional<raycast::RayHit> groundBelow(const raycast::CollisionMesh& mesh, raycast::Vec3 feet) {
    const raycast::Ray ray{.origin = raycast::Vec3{feet.x, feet.y, feet.z + kRayAbove},
                           .direction = raycast::Vec3{0.0F, 0.0F, -1.0F},
                           .length = kRayLength};
    return mesh.rayCast(ray, {}, 0);
}

} // namespace

void ShadowDim::step(bool hidden, std::uint32_t elapsedMs) {
    // The brain's stamp moves when the flag changes; the time since it counts up to the 250 ms the ramp lasts.
    m_sinceChangeMs = hidden != m_hidden ? 0 : std::min(kFallMs, m_sinceChangeMs + elapsedMs);
    m_hidden = hidden;
}

float ShadowDim::factor() const {
    // t runs from 0 to 0.5 over the ramp: falling from 1.0 while hidden, rising from 0.5 once out of the shadow.
    const float t = static_cast<float>(m_sinceChangeMs) / static_cast<float>(kFallMs) * (1.0F - kDimmest);
    return m_hidden ? 1.0F - t : kDimmest + t;
}

float humanDrawDistance() { return kHumanDrawDistance; }

float humanDistanceFade(float distance) {
    // A stand-in for the first second's fade-in after a human appears, which is not drawn (#1526): only the distance.
    return std::clamp((humanDrawDistance() - distance) / kHumanFadeLength, 0.0F, 1.0F);
}

HumanCull cullHuman(const world::CameraPose& camera, float halfWidth, float halfHeight, world::Vec3 feet, float scale) {
    const float radius = kHumanPoseRadius * std::max(1.0F, scale);
    const world::Vec3 to{feet.x - camera.position.x, feet.y + kHumanPoseLift - camera.position.y,
                         feet.z - camera.position.z};
    const auto along = [&to](world::Vec3 axis) { return (to.x * axis.x) + (to.y * axis.y) + (to.z * axis.z); };
    const float ahead = along(camera.forward);
    // A side plane through the eye has the normal (forward × half, -axis) of length sqrt(1 + half²): the centre is
    // outside it by more than the radius when its distance across passes the side's by radius × that length.
    const auto beyondSide = [ahead, radius](float across, float half) {
        return std::fabs(across) > (ahead * half) + (radius * std::sqrt(1.0F + (half * half)));
    };
    if (ahead < -radius || beyondSide(along(camera.right), halfWidth) || beyondSide(along(camera.up), halfHeight)) {
        return HumanCull::OutOfView;
    }
    // humanDistanceFade() is 0 from the draw distance on, for the drawn body's centre, which is inside the sphere.
    const float distance = std::sqrt(along(to));
    return distance > humanDrawDistance() + radius ? HumanCull::PastDrawDistance : HumanCull::MayShow;
}

bool onShadowGround(const raycast::CollisionMesh& mesh, raycast::Vec3 feet) {
    const std::optional<raycast::RayHit> hit = groundBelow(mesh, feet);
    return hit && (hit->flags & kTriangleShadow) != 0;
}

std::optional<BlobShadow> placeBlobShadow(const raycast::CollisionMesh& mesh, raycast::Vec3 feet, float size) {
    const std::optional<raycast::RayHit> hit = groundBelow(mesh, feet);
    if (!hit) {
        return std::nullopt;
    }
    const float groundZ = feet.z + kRayAbove - hit->t;
    const raycast::Vec3 n = hit->normal;
    return BlobShadow{
        .centre = raycast::Vec3{feet.x + n.x * kShadowLift, feet.y + n.y * kShadowLift, groundZ + n.z * kShadowLift},
        .normal = n,
        .size = size,
    };
}

} // namespace coney::graphics
