// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/way_clear.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "physics/contact.h"
#include "physics/mesh_sweep.h"
#include "raycast/collision_mesh.h"

namespace coney::human {

namespace {

// Whether a wall of `mesh` stops any sphere of the capsule swept from `feet` by `move`: its spheres of
// kWayClearRadius from one radius above the feet up to `height`, each swept as the walking sphere is
// (physics::sweepCapsuleMesh: walls the move goes into, 0.25 m tall or more).
bool wallOnTheWay(const raycast::CollisionMesh& mesh, anim::Vec3 feet, float height, anim::Vec3 move) {
    // The spheres' centres a diameter apart, the last no higher than one radius below the top (at least one).
    const float span = std::max(0.0F, height - (2.0F * kWayClearRadius));
    const int spheres = 1 + static_cast<int>(std::floor((span / (2.0F * kWayClearRadius)) + 1e-4F));
    physics::MeshScratch scratch;
    std::vector<physics::Contact> contacts;
    for (int i = 0; i < spheres; ++i) {
        const float up = kWayClearRadius + (2.0F * kWayClearRadius * static_cast<float>(i));
        const anim::Vec3 centre{feet.x, feet.y, feet.z + up};
        const anim::Vec3 reach{kWayClearRadius, kWayClearRadius, kWayClearRadius};
        const anim::Vec3 end = anim::add(centre, move);
        const physics::Box box{
            anim::subtract(anim::Vec3{std::min(centre.x, end.x), std::min(centre.y, end.y), std::min(centre.z, end.z)},
                           reach),
            anim::add(anim::Vec3{std::max(centre.x, end.x), std::max(centre.y, end.y), std::max(centre.z, end.z)},
                      reach)};
        const physics::CapsuleSweep sweep{
            .feet = feet, .radius = kWayClearRadius, .move = move, .footGap = up - kWayClearRadius};
        if (physics::sweepCapsuleMesh(mesh, box, sweep, scratch, contacts) > 0) {
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
    // A wall on the way to the goal blocks it.
    return mesh == nullptr || !wallOnTheWay(*mesh, from, height, anim::scale(move, reach));
}

} // namespace coney::human
