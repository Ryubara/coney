// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/rail_frame.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "camera/camera_lens.h"
#include "raycast/collision_mesh.h"

namespace coney::camera {

namespace {

using Distances = RailFrameDistances;

// The ray mask of the push's world rays (0x200).
constexpr std::uint32_t kRayMask = 0x200;

// The plane through `apex` with the inward direction `n` (any length but 0). @coney-only a maths helper
FramePlane planeThrough(anim::Vec3 apex, anim::Vec3 n) {
    const anim::Vec3 unit = anim::normalise(n);
    return FramePlane{.normal = unit, .w = anim::dot(unit, apex)};
}

// How far `p` is inside `plane`. @coney-only a maths helper
float insideOf(const FramePlane& plane, anim::Vec3 p) { return anim::dot(p, plane.normal) - plane.w; }

// A world ray from `origin` along the unit `direction`, `length` long: the distance to what it hits, or `length` when
// it hits nothing (or there is no mesh). @coney-only a wrapper over CollisionMesh::rayCast()
float rayReach(const raycast::CollisionMesh* mesh, anim::Vec3 origin, anim::Vec3 direction, float length) {
    if (mesh == nullptr) {
        return length;
    }
    const raycast::Ray ray{.origin = {origin.x, origin.y, origin.z},
                           .direction = {direction.x, direction.y, direction.z},
                           .length = length};
    const std::optional<raycast::RayHit> hit = mesh->rayCast(ray, {}, kRayMask);
    return hit ? hit->t : length;
}

// One edge's push of the point `a` (moved from `b`) that lies `inside` metres inside `plane`, short of `margin`: its
// move from `b` splits into the part along the plane's normal, which becomes the margin + the nudge inside, and the
// rest, which a ray from `b` as long as the rest + the margin cuts short to stop the margin (+ the nudge) before a
// wall.
/// @coney-only a helper of keepTargetInFrame (0x00140d68), which the original writes out for each edge
anim::Vec3 pushAcross(anim::Vec3 a, anim::Vec3 b, const FramePlane& plane, float inside, float margin,
                      const raycast::CollisionMesh* mesh) {
    const anim::Vec3 move = anim::subtract(a, b);
    anim::Vec3 across = anim::scale(plane.normal, anim::dot(move, plane.normal));
    const anim::Vec3 rest = anim::subtract(move, across);
    across = anim::add(across, anim::scale(plane.normal, margin - inside + Distances::kNudge));
    anim::Vec3 kept;
    // Stand-in (Coney's guard): a rest of zero length has no direction, so there is no ray; the original normalises
    // it anyway.
    if (const float restLength = anim::length(rest); restLength > 0.0F) {
        const anim::Vec3 direction = anim::scale(rest, 1.0F / restLength);
        const float reach = rayReach(mesh, b, direction, restLength + margin);
        kept = anim::scale(direction, reach - margin - Distances::kNudge);
    }
    return anim::add(anim::add(b, across), kept);
}

} // namespace

std::array<FramePlane, 3> framePlanes(const CameraView& view) {
    // In the camera's frame: across along +x, depth along +y, up along +z; a point is inside an edge while its offset
    // across (or up) is within the window's half width (or height) times its depth.
    const anim::Mat34 frame = anim::matrixFromQuat(view.orientation);
    const ViewWindow window =
        viewWindow(CameraLens{.fieldOfView = view.fieldOfView, .nearClip = view.nearClip, .farClip = view.farClip});
    const anim::Vec3 depth = anim::scale(frame.y, window.halfWidth);
    return {planeThrough(view.position, anim::add(frame.x, depth)),
            planeThrough(view.position, anim::subtract(depth, frame.x)),
            planeThrough(view.position, anim::add(frame.z, anim::scale(frame.y, window.halfHeight)))};
}

std::optional<anim::Vec3> keepTargetInFrame(const CameraView& view, const RailFrameTarget& target,
                                            const RailFrameRules& rules, const raycast::CollisionMesh* mesh) {
    // The highest and lowest of his feet now and at the start of his update; the ground ray starts over the highest by
    // the offset's height, at least 1 m.
    const float lowest = std::min(target.feet.z, target.before.z);
    const float highest = std::max(target.feet.z, target.before.z) + std::max(rules.offset.z, Distances::kMinHeadroom);
    // Stand-in for the loop over the targets: Coney's rail has one target, so the push runs once.
    anim::Vec3 a = anim::add(target.feet, rules.offset);
    const anim::Vec3 b = anim::add(target.before, rules.offset);
    const float radius = target.radius;
    const float startHeight = a.z;
    // Not ported (#1017): switch 2's push off the rail's segment planes (mode 0) or beyond the plane 2 m in front of
    // the camera (modes 1-3); Coney's rail does not keep switch 2.
    const std::array<FramePlane, 3> planes = framePlanes(view);
    bool moved = false;
    // 1. The sides: the left edge, else the right, by the side margin.
    for (std::size_t side = 0; side < 2; ++side) {
        if (const float inside = insideOf(planes.at(side), a); inside < rules.sideMargin) {
            a = pushAcross(a, b, planes.at(side), inside, rules.sideMargin, mesh);
            moved = true;
            break;
        }
    }
    // 2. The bottom edge, by his radius.
    if (const float inside = insideOf(planes[2], a); inside < radius) {
        a = pushAcross(a, b, planes[2], inside, radius, mesh);
        moved = true;
    }
    // 3. The line of sight, from 2 m in front of the camera at his starting height (the camera's in mode 3) toward
    // him: on a hit he is put his radius before the face. His height becomes the ray's start, hit or not.
    bool sightHit = false;
    if (moved || (rules.trailing && rules.joined)) {
        anim::Vec3 origin = anim::add(view.position, anim::scale(viewForward(view), Distances::kSightAhead));
        if (!rules.trailing) {
            origin.z = startHeight;
        }
        const anim::Vec3 toward = anim::subtract(a, origin);
        if (const float distance = anim::length(toward); distance > 0.0F && mesh != nullptr) {
            const anim::Vec3 direction = anim::scale(toward, 1.0F / distance);
            const float length = std::min(distance, Distances::kFarShare * view.farClip) + (Distances::kRadii * radius);
            const raycast::Ray ray{.origin = {origin.x, origin.y, origin.z},
                                   .direction = {direction.x, direction.y, direction.z},
                                   .length = length};
            const std::uint32_t mask = kRayMask | (rules.trailing ? 1U : 0U);
            if (const std::optional<raycast::RayHit> hit = mesh->rayCast(ray, {}, mask)) {
                const anim::Vec3 face{hit->normal.x, hit->normal.y, hit->normal.z};
                const float facing = -anim::dot(direction, face);
                // Stand-in (Coney's guard): a face met edge-on gives no stopping point; the two-sided test turns
                // every hit face toward the ray, so only a degenerate hit is skipped.
                if (facing > 0.0F) {
                    a = anim::add(origin, anim::scale(direction, hit->t - ((radius + Distances::kNudge) / facing)));
                    sightHit = true;
                }
            }
        }
        a.z = origin.z;
    }
    if (!moved && !sightHit) {
        return std::nullopt;
    }
    // 4. Back to his feet; his height from a ray straight down from over the highest feet, put just above what it hits
    // (kept when it hits nothing). Falling, he takes only 30% of the change.
    anim::Vec3 feet = anim::subtract(a, rules.offset);
    const float kept = feet.z;
    const float drop = highest - lowest + Distances::kMinHeadroom;
    if (const float reach = rayReach(mesh, anim::Vec3{feet.x, feet.y, highest}, anim::Vec3{0.0F, 0.0F, -1.0F}, drop);
        reach < drop) {
        feet.z = highest - (reach - Distances::kAboveGround);
    }
    if (target.falling) {
        // Not ported (#1017): the original lands him first (Human_Land(1, 1)).
        feet.z = kept + ((feet.z - kept) * Distances::kFallShare);
    }
    return feet;
}

} // namespace coney::camera
