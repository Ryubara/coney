// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/perception.h"

#include <array>
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

ClearRay clearRayTo(const raycast::CollisionMesh* mesh, anim::Vec3 feet, anim::Vec3 point, std::uint32_t mask) {
    if (mesh == nullptr) {
        return ClearRay{.clear = true};
    }
    const anim::Vec3 from = raised(feet, kEyeHeight);
    const anim::Vec3 way = anim::subtract(raised(point, 0.1F), from);
    const float length = anim::length(way);
    if (length < 1e-5F) {
        return ClearRay{.clear = true};
    }
    const anim::Vec3 direction = anim::scale(way, 1.0F / length);
    const raycast::Ray ray{
        .origin = {from.x, from.y, from.z}, .direction = {direction.x, direction.y, direction.z}, .length = length};
    if (mesh->rayCast(ray, kSightSeeThrough, mask)) {
        return ClearRay{};
    }
    // The same ray against the panes alone: every material but GLASS left out.
    static const std::array<std::uint8_t, 255> kAllButGlass = [] {
        std::array<std::uint8_t, 255> list{};
        std::uint8_t next = 0;
        for (int material = 0; material < 256; ++material) {
            if (material != kGlassMaterial) {
                list.at(next++) = static_cast<std::uint8_t>(material);
            }
        }
        return list;
    }();
    // Coney's choice: Ray_IsClear's result GLASS read as any GLASS triangle on the ray or kGlassEndSlack past it.
    raycast::Ray probe = ray;
    probe.length = length + kGlassEndSlack;
    return ClearRay{.clear = true, .throughGlass = mesh->rayCast(probe, kAllButGlass, mask).has_value()};
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
