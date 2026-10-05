// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/climb.h"

#include <cmath>

namespace coney::human {

namespace {

// The type of a clip event whose vector is the climb's reach.
constexpr std::uint16_t kReachEvent = 8;
// The running form of a climb is three clips after the standing one.
constexpr std::uint32_t kRunningOffset = 3;

raycast::Vec3 toMesh(anim::Vec3 v) { return raycast::Vec3{v.x, v.y, v.z}; }
anim::Vec3 fromMesh(raycast::Vec3 v) { return anim::Vec3{v.x, v.y, v.z}; }

// A horizontal ray from `height` above the feet along `direction`, `length` long.
std::optional<raycast::RayHit> castAhead(const raycast::CollisionMesh& mesh, anim::Vec3 feet, anim::Vec3 direction,
                                         float height, float length) {
    const raycast::Ray ray{.origin = toMesh(anim::add(feet, anim::Vec3{0.0F, 0.0F, height})),
                           .direction = toMesh(direction),
                           .length = length};
    return mesh.rayCast(ray, {}, 0);
}

} // namespace

ClimbTuning& climbTuning() {
    static ClimbTuning tuning;
    return tuning;
}

std::uint32_t climbFirstClip(ClimbKind kind, bool running) {
    std::uint32_t id = 437; // ANIM_FENCE_CLIMB_STANDING_01
    switch (kind) {
    case ClimbKind::Fence:
        break;
    case ClimbKind::ShortFence:
        id = 443;
        break;
    case ClimbKind::Wall:
        id = 449;
        break;
    case ClimbKind::ShortWall:
        id = 455;
        break;
    }
    return running ? id + kRunningOffset : id;
}

bool climbable(const raycast::RayHit& hit, bool player) {
    return hit.material == kClimbableMaterial || (hit.flags & kTriangleClimbable) != 0 ||
           (player && (hit.flags & kTrianglePlayerClimbable) != 0);
}

std::optional<ClimbProbe> probeClimb(const raycast::CollisionMesh& mesh, anim::Vec3 feet, anim::Vec3 direction,
                                     float length, bool player) {
    const ClimbTuning& tuning = climbTuning();
    // 1. The two forward rays: a face both meet at about the same distance is tall; the low ray alone, short.
    const auto high = castAhead(mesh, feet, direction, tuning.highProbe, length);
    const auto low = castAhead(mesh, feet, direction, tuning.lowProbe, length);
    if (!low) {
        return std::nullopt;
    }
    bool tall = false;
    raycast::RayHit hit = *low;
    if (high && std::abs(high->t - low->t) < tuning.tallTolerance) {
        tall = true;
        hit = *high;
    }
    if (!climbable(hit, player)) {
        return std::nullopt;
    }
    // 2. The face must look at the climber.
    const anim::Vec3 normal = fromMesh(hit.normal);
    if (anim::dot(normal, direction) >= tuning.facing) {
        return std::nullopt;
    }
    // 3. The top just behind the face: a ray straight down from H above the point 0.4 m past it, H + 0.5 long.
    const float probeHeight = tall ? tuning.tallProbeHeight : tuning.shortProbeHeight;
    const anim::Vec3 behind =
        anim::subtract(anim::add(feet, anim::scale(direction, hit.t)), anim::scale(normal, tuning.behind));
    const raycast::Ray down{.origin = toMesh(anim::Vec3{behind.x, behind.y, feet.z + probeHeight}),
                            .direction = raycast::kDown,
                            .length = probeHeight + 0.5F};
    std::optional<float> top;
    if (const auto ground = mesh.rayCast(down, {}, 0); ground) {
        top = probeHeight - ground->t;
    }
    ClimbProbe probe{.kind = tall ? ClimbKind::Fence : ClimbKind::ShortFence,
                     .distance = hit.t,
                     .normal = normal,
                     .face = anim::add(feet, anim::scale(direction, hit.t)),
                     .top = top.value_or(0.0F)};
    // 4. A top inside the window is a wall; any other top from 0.25 m up refuses.
    const float windowLow = tall ? tuning.tallWindowLow : tuning.shortWindowLow;
    const float windowHigh = tall ? tuning.tallWindowHigh : tuning.shortWindowHigh;
    if (top && *top >= windowLow && *top <= windowHigh) {
        probe.kind = tall ? ClimbKind::Wall : ClimbKind::ShortWall;
        return probe;
    }
    if (top && *top >= tuning.fenceTopLimit) {
        return std::nullopt;
    }
    // 5. A fence: nothing to stand on behind it. A tall one must be lower than 2.5 m.
    if (tall && castAhead(mesh, feet, direction, tuning.fenceCeiling, length)) {
        return std::nullopt;
    }
    return probe;
}

bool climbableAhead(const raycast::CollisionMesh& mesh, anim::Vec3 feet, anim::Vec3 direction, float length,
                    bool player) {
    const auto hit = castAhead(mesh, feet, direction, climbTuning().highProbe, length);
    return hit && climbable(*hit, player);
}

bool faceAhead(const raycast::CollisionMesh& mesh, anim::Vec3 feet, anim::Vec3 direction, float length) {
    return castAhead(mesh, feet, direction, climbTuning().lowProbe, length).has_value();
}

float clipReach(const anim::AnimClip& clip) {
    for (const anim::ClipEvent& event : clip.events) {
        if (event.type == kReachEvent) {
            return anim::length(event.position);
        }
    }
    return anim::length(clip.displacement);
}

bool withinClimbReach(float distance, float standingReach, float runningReach, bool running) {
    const ClimbTuning& tuning = climbTuning();
    const float nearLimit = (standingReach + runningReach) * tuning.standShare;
    if (running) {
        return distance >= nearLimit && distance <= runningReach * tuning.runShare;
    }
    return distance <= nearLimit;
}

} // namespace coney::human
