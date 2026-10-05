// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "combat/anim_ranges.h"

// Where the two bodies of a paired move stand: the grab's alignment before its connecting clips (the grabber turned and
// slid so that the victim is at the clip's reach straight ahead, the victim turned to face it or away), the gate and
// the snap at the clips' end (the victim put at the hold's offset in the grabber's frame), and the check that a move in
// the hold finds the victim in its place. Every offset is in the grabber's frame, x to its right and y ahead, and comes
// from the Anim Range List's direction × reach of the grabber's anim id. Pure functions of their arguments.
// Research: docs/research/combat.md#grab-posing

namespace coney::human {

/// The front hold's offset (82: (0.351, 0.936) × 1.081 m) and the rear hold's (84: (−0.399, 0.916) × 0.242 m), for an
/// Anim Range List without the record.
inline constexpr anim::Vec3 kFrontHoldOffset{0.380F, 1.012F, 0.0F};
inline constexpr anim::Vec3 kRearHoldOffset{-0.097F, 0.222F, 0.0F};
/// The mounted victim's offset under a tackle, from the record of 210 / 213 ((−0.965, 0.259) × 0.124 m). **Inferred**:
/// how the tackle places the pair is not traced, only the record is read.
inline constexpr anim::Vec3 kMountOffset{-0.120F, 0.032F, 0.0F};
/// The connecting clips' reach (72 from the front, 74 from the rear) and far range, for a list without them.
inline constexpr float kConnectReachFront = 0.999F;
inline constexpr float kConnectReachRear = 1.018F;
inline constexpr float kConnectFarRange = 2.5F;
/// A player's far range is the clip's × 1.25 (the controller kind 0, inferred to be a player).
inline constexpr float kPlayerFarScale = 1.25F;
/// The alignment lasts this share of the clip's time to its first contact event (or of its duration).
inline constexpr float kAlignShare = 0.1F;
/// The alignment's turn is skipped below this angle (radians) and its slide below this distance (metres, snapped).
inline constexpr float kAlignMinTurn = 0.01F;
inline constexpr float kAlignMinSlide = 0.01F;
/// The gate at the connecting clips' end: the grab is released when the victim is beyond the larger of the hold's
/// reach + kGateMargin and its reach × kGateScale, or more than kGateHeight above or below.
inline constexpr float kGateMargin = 0.2F;
inline constexpr float kGateScale = 1.2F;
inline constexpr float kGateHeight = 0.2F;
/// A move in the hold is refused unless the victim stands within this distance of the move's point.
inline constexpr float kPairPlaceTolerance = 0.3F;

/// `local` (x right, y ahead, z up of a human at `feet` facing `heading`) in world axes.
[[nodiscard]] anim::Vec3 fromFrame(anim::Vec3 feet, float heading, anim::Vec3 local);
/// `world` in the frame of a human at `feet` facing `heading` (the inverse of fromFrame()).
[[nodiscard]] anim::Vec3 toFrame(anim::Vec3 feet, float heading, anim::Vec3 world);

/// Where anim `id` puts the partner in the attacker's frame: its Anim Range List record's direction × reach, or
/// `fallback` when `ranges` is null or has no reach for it.
[[nodiscard]] anim::Vec3 pairPoint(const combat::AnimRangeList* ranges, std::uint32_t id, anim::Vec3 fallback);

/// How long the alignment before `clip` (played at `rate`) lasts: kAlignShare × the time of its first contact event
/// (types 9, 0xf, 0x13, 0x2c, 0x34, 0x36, 0x41) or, with none, its duration, over the rate.
/// @orig 0x00101658 Anim_FirstContactTime (unknown)
[[nodiscard]] float alignSeconds(const anim::AnimClip& clip, float rate);

/// The alignment of a pair before its connecting clips.
struct PairAlignment {
    bool inRange = false;     ///< The victim is within the far range; otherwise the move fails.
    float grabberHeading = 0; ///< The grabber turns to face the victim...
    anim::Vec3 grabberFeet;   ///< ... and slides here, the victim at the reach straight ahead (at the victim's height).
    float victimHeading = 0;  ///< The victim turns to face the grabber (front) or away from it (rear).
};

/// The alignment of a grabber at `grabber` and a victim at `victim` for a connecting clip of `reach` and `farRange`
/// metres (the far range already scaled for a player), from the victim's `rear` or front. The victim does not move.
/// @orig 0x00276998 Pair_AlignStart (unknown)
[[nodiscard]] PairAlignment alignPair(anim::Vec3 grabber, anim::Vec3 victim, float reach, float farRange, bool rear);

/// Whether the victim at `victim` is close enough to the grabber at `grabber` for the hold of reach `holdReach` to
/// take it at the connecting clips' end (Grab_ConnectEnd's gate).
/// @orig 0x0026bad8 Grab_ConnectEnd (unknown)
[[nodiscard]] bool holdGatePasses(anim::Vec3 grabber, anim::Vec3 victim, float holdReach);

/// Whether the victim at `victim` stands within kPairPlaceTolerance of `point` in the frame of the attacker at
/// `attacker` facing `heading`.
/// @orig 0x00277958 Pair_CheckPlace (unknown)
[[nodiscard]] bool pairInPlace(anim::Vec3 attacker, float heading, anim::Vec3 victim, anim::Vec3 point);

} // namespace coney::human
