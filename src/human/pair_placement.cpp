// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/pair_placement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "human/locomotion.h"

namespace coney::human {

namespace {

// The clip event types that count as a contact for the alignment's time (docs/research/combat.md#grab-posing).
constexpr std::array<std::uint16_t, 7> kContactEvents{0x09, 0x0f, 0x13, 0x2c, 0x34, 0x36, 0x41};

} // namespace

anim::Vec3 fromFrame(anim::Vec3 feet, float heading, anim::Vec3 local) {
    // Right is (cos h, sin h), ahead (-sin h, cos h): the frame turned by the heading.
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    return anim::Vec3{feet.x + (local.x * c) - (local.y * s), feet.y + (local.x * s) + (local.y * c), feet.z + local.z};
}

anim::Vec3 toFrame(anim::Vec3 feet, float heading, anim::Vec3 world) {
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    const anim::Vec3 d = anim::subtract(world, feet);
    return anim::Vec3{(d.x * c) + (d.y * s), (-d.x * s) + (d.y * c), d.z};
}

anim::Vec3 pairPoint(const combat::AnimRangeList* ranges, std::uint32_t id, anim::Vec3 fallback) {
    const combat::AnimRange* range = ranges != nullptr ? ranges->find(id) : nullptr;
    if (range == nullptr || range->reach <= 0.0F) {
        return fallback;
    }
    return anim::Vec3{range->directionX * range->reach, range->directionY * range->reach, 0.0F};
}

float alignSeconds(const anim::AnimClip& clip, float rate) {
    // The first contact event's frame, or the whole clip.
    float seconds = clip.duration;
    for (const anim::ClipEvent& event : clip.events) {
        if (std::ranges::find(kContactEvents, event.type) != kContactEvents.end()) {
            seconds = std::min(seconds, static_cast<float>(event.frame) / anim::kClipFrameRate);
        }
    }
    return rate > 0.0F ? kAlignShare * seconds / rate : 0.0F;
}

PairAlignment alignPair(anim::Vec3 grabber, anim::Vec3 victim, float reach, float farRange, bool rear) {
    PairAlignment align;
    const anim::Vec3 to = anim::subtract(victim, grabber);
    align.inRange = anim::length(to) <= farRange;
    // The grabber faces the victim and stands `reach` from it, at its height; the victim faces back, or away.
    align.grabberHeading = std::hypot(to.x, to.y) > 1e-4F ? headingOf(to) : 0.0F;
    align.grabberFeet = anim::subtract(victim, anim::scale(facing(align.grabberHeading), reach));
    align.grabberFeet.z = victim.z;
    align.victimHeading = wrapAngle(rear ? align.grabberHeading : align.grabberHeading + std::numbers::pi_v<float>);
    return align;
}

bool holdGatePasses(anim::Vec3 grabber, anim::Vec3 victim, float holdReach) {
    const float limit = std::max(holdReach + kGateMargin, holdReach * kGateScale);
    return anim::distance(grabber, victim) <= limit && std::fabs(victim.z - grabber.z) <= kGateHeight;
}

bool pairInPlace(anim::Vec3 attacker, float heading, anim::Vec3 victim, anim::Vec3 point) {
    return anim::distance(fromFrame(attacker, heading, point), victim) <= kPairPlaceTolerance;
}

} // namespace coney::human
