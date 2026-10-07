// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/human_lighting.h"

#include <algorithm>

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

float humanDistanceFade(float distance) {
    return std::clamp((kHumanDrawDistance - distance) / kHumanFadeLength, 0.0F, 1.0F);
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
