// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/perception.h"

#include <cmath>
#include <optional>

#include "human/human.h"
#include "human/locomotion.h"
#include "raycast/collision_mesh.h"

namespace coney::ai {

namespace {

// Casts from `from` to `to` through `mesh` with the sight list; the hit, if any, before reaching `to`.
std::optional<raycast::RayHit> castSight(const raycast::CollisionMesh& mesh, anim::Vec3 from, anim::Vec3 to) {
    const anim::Vec3 way = anim::subtract(to, from);
    const float length = anim::length(way);
    if (length < 1e-5F) {
        return std::nullopt;
    }
    const anim::Vec3 direction = anim::scale(way, 1.0F / length);
    const raycast::Ray ray{
        .origin = {from.x, from.y, from.z}, .direction = {direction.x, direction.y, direction.z}, .length = length};
    return mesh.rayCast(ray, kSightSeeThrough, 0);
}

// The point `height` above `feet`.
anim::Vec3 raised(anim::Vec3 feet, float height) { return anim::Vec3{feet.x, feet.y, feet.z + height}; }

} // namespace

SightLine lineOfSight(const raycast::CollisionMesh* mesh, anim::Vec3 from, anim::Vec3 to) {
    if (mesh == nullptr) {
        return SightLine{.clear = true};
    }
    // Eye to eye; when that is blocked, eye to chest.
    const anim::Vec3 eye = raised(from, kEyeHeight);
    const std::optional<raycast::RayHit> first = castSight(*mesh, eye, raised(to, kEyeHeight));
    if (!first) {
        return SightLine{.clear = true};
    }
    const bool second = !castSight(*mesh, eye, raised(to, kChestHeight)).has_value();
    return SightLine{.clear = second, .crossedMaterial = first->material};
}

bool inFieldOfView(float fieldOfView, const human::Human& human, anim::Vec3 point) {
    const anim::Vec3 to = anim::subtract(point, human.position());
    const float distance = std::hypot(to.x, to.y);
    if (distance < 1e-5F) {
        return true;
    }
    const anim::Vec3 facing = human::facing(human.heading());
    const float cosine = (facing.x * to.x + facing.y * to.y) / distance;
    return cosine >= std::cos(fieldOfView * 0.5F);
}

bool shadowAllowsSight(const human::Human& viewer, const human::Human& other) {
    if (!other.hidden()) {
        return true;
    }
    return anim::distance(viewer.position(), other.position()) <= kHiddenSightRange && viewer.onShadowGround();
}

bool canSeeHuman(const raycast::CollisionMesh* mesh, const human::Human& viewer, const human::Human& other,
                 float range) {
    if (anim::distance(viewer.position(), other.position()) > range || !shadowAllowsSight(viewer, other)) {
        return false;
    }
    return lineOfSight(mesh, viewer.position(), other.position()).clear;
}

} // namespace coney::ai
