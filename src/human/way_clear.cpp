// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/way_clear.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "human/body.h"
#include "raycast/collision_mesh.h"

namespace coney::human {

namespace {

// How far apart along the way the walls are tested, m, and the spheres up the capsule.
constexpr float kWallStep = 0.1F;

// Whether a wall of `mesh` that the move `move` goes into stops a sphere of the capsule anywhere at `feet`: its
// spheres of kWayClearRadius from just above the walking sweep's low walls up to `height`.
bool wallAt(const raycast::CollisionMesh& mesh, anim::Vec3 feet, float height, anim::Vec3 move,
            std::vector<std::uint16_t>& scratch) {
    const WallFilter filter{.move = move, .skipLow = true, .excludeMaterials = {}};
    // The spheres' centres from one radius above the feet, a diameter apart, the last no higher than one radius
    // below the top (at least one).
    const float span = std::max(0.0F, height - (2.0F * kWayClearRadius));
    const int spheres = 1 + static_cast<int>(std::floor((span / (2.0F * kWayClearRadius)) + 1e-4F));
    for (int i = 0; i < spheres; ++i) {
        const float up = kWayClearRadius + (2.0F * kWayClearRadius * static_cast<float>(i));
        const anim::Vec3 centre{feet.x, feet.y, feet.z + up};
        if (nearestWallPush(mesh, centre, kWayClearRadius, filter, scratch).has_value()) {
            return true;
        }
    }
    return false;
}

} // namespace

bool wayClear(const raycast::CollisionMesh* mesh, anim::Vec3 from, float height, const UprightCapsule& goal,
              std::span<const UprightCapsule> others) {
    const UprightCapsule mover{.feet = from, .top = height, .radius = kWayClearRadius};
    const anim::Vec3 move = anim::subtract(goal.feet, from);
    // Where the sweep meets the goal (the whole way when it does not).
    const std::optional<CapsuleContact> atGoal = sweepCapsules(mover, move, goal);
    const float reach = atGoal ? atGoal->fraction : 1.0F;
    // Any other human met first blocks it.
    for (const UprightCapsule& other : others) {
        if (const std::optional<CapsuleContact> met = sweepCapsules(mover, move, other); met && met->fraction < reach) {
            return false;
        }
    }
    if (mesh == nullptr) {
        return true;
    }
    // A wall on the way to the goal blocks it.
    const float length = std::hypot(move.x, move.y) * reach;
    const int samples = std::max(1, static_cast<int>(std::ceil(length / kWallStep)));
    std::vector<std::uint16_t> scratch;
    for (int i = 0; i <= samples; ++i) {
        const float t = reach * static_cast<float>(i) / static_cast<float>(samples);
        if (wallAt(*mesh, anim::add(from, anim::scale(move, t)), height, move, scratch)) {
            return false;
        }
    }
    return true;
}

} // namespace coney::human
