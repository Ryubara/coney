// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/body.h"

#include <cstddef>

#include "physics/mesh_sweep.h"
#include "physics/sphere_triangle.h"

namespace coney::human {

BodyTuning& bodyTuning() {
    static BodyTuning tuning;
    return tuning;
}

float walkingRadius(float scale) { return bodyTuning().radius * scale; }

float walkingCentreHeight(float scale) { return walkingRadius(scale) + bodyTuning().footGap; }

float playerWalkingRadius(float scale) { return walkingRadius(scale * bodyTuning().playerFactor) * scale; }

float playerWalkingCentreHeight(float scale) { return playerWalkingRadius(scale) + bodyTuning().footGap; }

float aiWalkingRadius(float scale) { return walkingRadius(scale) * scale; }

WallClass wallClassOf(const raycast::CollisionMesh& mesh, std::uint16_t index) {
    if ((mesh.triangles()[index].flags & raycast::kTriangleEnabled) == 0) {
        return WallClass::Disabled;
    }
    physics::WorkingTriangle t;
    const auto vertices = mesh.vertices();
    for (std::size_t i = 0; i < 3; ++i) {
        const raycast::Vec3 v = vertices[mesh.triangles()[index].vertices.at(i)];
        t.corners.at(i) = anim::Vec3{v.x, v.y, v.z};
    }
    physics::computeNormal(t);
    if (anim::length(t.normal) == 0.0F) {
        return WallClass::Degenerate;
    }
    if (!physics::capsuleWallNormal(t.normal)) {
        return t.normal.z > 0.0F ? WallClass::Floor : WallClass::Ceiling;
    }
    return physics::capsuleLowOrThin(mesh, index, bodyTuning().minWallHeight) ? WallClass::LowWall : WallClass::Wall;
}

} // namespace coney::human
