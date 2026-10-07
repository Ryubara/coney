// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_reach.h"

#include <array>
#include <cstddef>

#include "combat/anim_ranges.h"
#include "human/fighter.h"

namespace coney::ai {

namespace {

// The anims of kind 0: the run attack, the walk attack, the X1 and the grounded strike.
constexpr std::uint32_t kRunAttack = 24;
constexpr std::uint32_t kWalkAttack = 23;
constexpr std::uint32_t kX1 = 11;
constexpr std::uint32_t kS1 = 12;
constexpr std::uint32_t kGroundedCross = 194;
// The throw.
constexpr int kThrowKind = 23;
// The far reach's clips of kinds 1-9 (each its own first clip).
constexpr std::array<std::uint32_t, 9> kFarStrikeClips{12, 13, 15, 14, 16, 17, 19, 18, 20};

// A source from an anim.
ReachSource anim(std::uint32_t id) { return ReachSource{.anim = id, .metres = 0.0F}; }
// A fixed distance.
ReachSource fixed(float metres) { return ReachSource{.anim = std::nullopt, .metres = metres}; }

// The clip of the kinds both reaches share (10-17, 19-22, 32), with the grab's own; none for the others.
std::optional<std::uint32_t> specialClip(int kind, std::uint32_t grabClip) {
    switch (kind) {
    case 10:
        return 25;
    case 11:
        return 21;
    case 12:
        return 193;
    case 13:
        return 194;
    case 14:
        return 237;
    case 15:
        return 664;
    case 16:
        return 653;
    case 17:
        return 657;
    case 19:
        return 0;
    case 20:
        return 1;
    case 21:
        return 3;
    case 22:
        return grabClip;
    case 32:
        return 104;
    default:
        return std::nullopt;
    }
}

// Whether a held object changes `kind`'s reach: every kind but those with a clip of their own and the throw.
bool heldChanges(int kind) { return kind != kThrowKind && !specialClip(kind, 0).has_value(); }

// The near reach with an object of `set` in hand (`0x00231a80`): a throwable thrown from range or swung, the bat and
// bottle sets' own swings, the ranged sets' fixed reach, 2 m otherwise.
ReachSource heldNear(const ReachInput& in) {
    const int k = in.kind;
    switch (in.heldSet) {
    case 1:
        if ((k == 0 || k == 1) && !in.dealer && in.beyondNearRange) {
            return fixed(kThrowReach);
        }
        if (k == 0) {
            return anim(47);
        }
        return k == 1 || k == 5 ? anim(45) : fixed(kNoClipReach);
    case 2:
        if (k == 0) {
            return anim(41);
        }
        return k >= 1 && k <= 5 ? anim(39) : fixed(kNoClipReach);
    case 3:
        if (k == 0) {
            return anim(36);
        }
        return k == 1 || k == 5 ? anim(34) : fixed(kNoClipReach);
    case 4:
        return fixed(k <= 9 ? kSet4Reach : kNoClipReach);
    case 5:
        return fixed(k <= 9 ? kSet5Reach : kNoClipReach);
    default:
        return fixed(kNoClipReach);
    }
}

// The far reach with an object of `set` in hand: the throwable's range, the ranged sets, and sets 1-3's own clips for
// kinds 0, 1 and 5.
ReachSource heldFar(const ReachInput& in) {
    const int k = in.kind;
    if (in.heldSet == 1 && (k == 0 || k == 1) && !in.dealer && in.beyondNearRange) {
        return fixed(kThrowReach);
    }
    if (in.heldSet == 4 || in.heldSet == 5) {
        return fixed(k > 9 ? kNoClipReach : (in.heldSet == 4 ? kSet4Reach : kSet5Reach));
    }
    // Sets 3, 2 and 1: kinds 0, 1 and 5.
    constexpr std::array<std::array<std::uint32_t, 3>, 3> kSetClips{{{47, 45, 46}, {41, 39, 40}, {36, 34, 35}}};
    if (in.heldSet >= 1 && in.heldSet <= 3 && (k == 0 || k == 1 || k == 5)) {
        const auto& clips = kSetClips[static_cast<std::size_t>(in.heldSet - 1)];
        return anim(clips[k == 0 ? 0U : (k == 1 ? 1U : 2U)]);
    }
    return fixed(kNoClipReach);
}

// Kind 0's moving clips: the run attack at a run or sprint, the walk attack at a walk.
std::optional<std::uint32_t> movingX1(const ReachInput& in) {
    if (in.running) {
        return kRunAttack;
    }
    if (in.walking) {
        return kWalkAttack;
    }
    return std::nullopt;
}

// A record's value, or the default strike reach without one.
float recordValue(const ReachSource& source, const combat::AnimRangeList* ranges, bool far) {
    if (!source.anim.has_value()) {
        return source.metres;
    }
    const combat::AnimRange* range = ranges != nullptr ? ranges->find(*source.anim) : nullptr;
    if (range == nullptr || range->reach <= 0.0F) {
        return human::kDefaultStrikeReach;
    }
    return far ? ranges->farRange(*source.anim) : range->reach;
}

} // namespace

ReachSource nearReachSource(const ReachInput& input) {
    const int k = input.kind;
    if (input.heldSet != 0 && heldChanges(k)) {
        return heldNear(input);
    }
    if (k == kThrowKind) {
        return fixed(kThrowReach);
    }
    if (k == 0) {
        return anim(movingX1(input).value_or(input.targetDown ? kGroundedCross : kX1));
    }
    if (k == 1 || k == 3 || (k >= 5 && k <= 9)) {
        return anim(kS1);
    }
    if (k == 2 || k == 4) {
        return anim(kX1);
    }
    constexpr std::uint32_t kGrabNear = 72;
    if (const std::optional<std::uint32_t> clip = specialClip(k, kGrabNear); clip.has_value()) {
        return anim(*clip);
    }
    return fixed(kNoClipReach);
}

ReachSource farReachSource(const ReachInput& input) {
    const int k = input.kind;
    if (input.heldSet != 0 && heldChanges(k)) {
        return heldFar(input);
    }
    if (k == kThrowKind) {
        return fixed(kThrowReach);
    }
    if (k == 0) {
        return anim(movingX1(input).value_or(kX1));
    }
    if (k >= 1 && k <= 9) {
        return anim(kFarStrikeClips[static_cast<std::size_t>(k - 1)]);
    }
    constexpr std::uint32_t kGrabFar = 70;
    if (const std::optional<std::uint32_t> clip = specialClip(k, kGrabFar); clip.has_value()) {
        return anim(*clip);
    }
    return fixed(kNoClipReach);
}

float nearReach(const ReachSource& source, const combat::AnimRangeList* ranges) {
    return recordValue(source, ranges, false);
}

float farReach(const ReachSource& source, const combat::AnimRangeList* ranges) {
    return recordValue(source, ranges, true);
}

} // namespace coney::ai
