// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_services.h"

#include <cmath>
#include <numbers>

#include "core/game_random.h"
#include "raycast/collision_mesh.h"

namespace coney::world_objects {

void setTrianglesEnabled(raycast::CollisionMesh* mesh, std::span<const std::uint32_t> triangles, bool on) {
    if (mesh == nullptr) {
        return;
    }
    const std::span<raycast::CollisionTriangle> all = mesh->mutableTriangles();
    for (const std::uint32_t index : triangles) {
        if (index >= all.size()) {
            continue;
        }
        std::uint16_t& flags = all[index].flags;
        flags = on ? static_cast<std::uint16_t>(flags | raycast::kTriangleEnabled)
                   : static_cast<std::uint16_t>(flags & ~raycast::kTriangleEnabled);
    }
}

void markTriangles(raycast::CollisionMesh* mesh, std::span<const std::uint32_t> triangles, std::uint16_t flags,
                   int material) {
    if (mesh == nullptr) {
        return;
    }
    const std::span<raycast::CollisionTriangle> all = mesh->mutableTriangles();
    for (const std::uint32_t index : triangles) {
        if (index >= all.size()) {
            continue;
        }
        all[index].flags = static_cast<std::uint16_t>(all[index].flags | flags);
        if (material >= 0) {
            all[index].material = static_cast<std::uint8_t>(material);
        }
    }
}

int randomBelow(GameRandom* random, int n) {
    if (random == nullptr || n <= 0) {
        return 0;
    }
    return random->range(0, n - 1);
}

anim::Quat turnAboutVertical(anim::Quat rotation, float degrees) {
    // The turn's half-angle quaternion about z, applied in the object's frame: rotation × turn.
    const float half = degrees * std::numbers::pi_v<float> / 360.0F;
    const anim::Quat turn{0.0F, 0.0F, std::sin(half), std::cos(half)};
    const anim::Quat& a = rotation;
    return anim::normalise(anim::Quat{a.w * turn.x + a.x * turn.w + a.y * turn.z - a.z * turn.y,
                                      a.w * turn.y - a.x * turn.z + a.y * turn.w + a.z * turn.x,
                                      a.w * turn.z + a.x * turn.y - a.y * turn.x + a.z * turn.w,
                                      a.w * turn.w - a.x * turn.x - a.y * turn.y - a.z * turn.z});
}

anim::Vec3 rotate(anim::Quat rotation, anim::Vec3 v) {
    return anim::transformDirection(anim::matrixFromQuat(rotation), v);
}

} // namespace coney::world_objects
