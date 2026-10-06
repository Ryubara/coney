// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "combat/anim_ids.h"
#include "combat/attacks.h"
#include "human/human_animator.h"

// The clips the fighter plays, by anim id, shared by its source files (fighter.cpp, fighter_grab.cpp,
// fighter_victim.cpp), with the small helpers that build their lists.
// Research: docs/research/combat.md, docs/references/anim-ids.md

namespace coney::human::clips {

inline constexpr std::uint32_t kIdle = 388;
inline constexpr std::uint32_t kGrabMiss = 69;
inline constexpr std::uint32_t kGrabFrontEnd = 72;
inline constexpr std::uint32_t kGrabReactFromFront = 73;
inline constexpr std::uint32_t kGrabRearEnd = 74;
inline constexpr std::uint32_t kGrabReactFromRear = 75;
inline constexpr std::uint32_t kGrabHold = 82;
inline constexpr std::uint32_t kGrabHeld = 83;
inline constexpr std::uint32_t kGrabRearHold = 84;
inline constexpr std::uint32_t kGrabRearHeld = 85;
inline constexpr std::uint32_t kGrabLetGoReact = 94;
// A pair broken from outside (docs/research/combat.md#pair-break): the clip the human not placed plays.
inline constexpr std::uint32_t kBreakRearGrabber = 106;   ///< GRAB_REAR_BREAK: the grabber of a rear grab or mugging
inline constexpr std::uint32_t kBreakRearVictim = 107;    ///< GRAB_REAR_BREAK_REACT: its victim
inline constexpr std::uint32_t kBreakFrontGrabber = 138;  ///< GRAB_FRONT_HIT_VICTIM_KNOCKDOWN_REACT: front grabber
inline constexpr std::uint32_t kBreakFrontVictim = 145;   ///< GRAB_FRONT_HIT_GRABBER_KNOCKDOWN_REACT: its victim
inline constexpr std::uint32_t kBreakMounter = 244;       ///< MOUNT_RELEASE: the human on top
inline constexpr std::uint32_t kBreakMountedVictim = 245; ///< MOUNT_RELEASE_REACT: the one below, then 199
inline constexpr std::uint32_t kNormalFromFight = 389;
inline constexpr std::uint32_t kTackleMiss = 2;
inline constexpr std::uint32_t kTackleHit = 5;
inline constexpr std::uint32_t kTackleReact = 6;
inline constexpr std::uint32_t kMountedIdle = 207;
inline constexpr std::uint32_t kMountingIdle = 210;
inline constexpr std::uint32_t kGroundedIdle = 196;
inline constexpr std::uint32_t kGroundedStrikeReact = 195;
inline constexpr std::uint32_t kGroundedRise = 199;
inline constexpr std::uint32_t kMugIntro = 338;
inline constexpr std::uint32_t kMugIntroReact = 339;
inline constexpr std::uint32_t kMugLoop = 340;
inline constexpr std::uint32_t kMugLoopReact = 341;
inline constexpr std::uint32_t kMugStruggle = 342;
inline constexpr std::uint32_t kMugStruggleReact = 343;
inline constexpr std::uint32_t kMugEnd = 344;
inline constexpr std::uint32_t kMugEndReact = 345;
inline constexpr std::uint32_t kBlockSustain = 606;
inline constexpr std::uint32_t kStereoStealIntro = 683; ///< `STEREO_STEAL_INTRO`.
inline constexpr std::uint32_t kStereoStealLoop = 684;  ///< The stereo theft's loop while the stick turns.
inline constexpr std::uint32_t kBlockShuffle = 607;

/// What the player's moves hold on the record `+0x08` while their clips play (docs/research/tasks.md#held-flags).
/// An attack holds its phases and starts in its wind-up, as `Attack_Start` builds it; the grab's and tackle's clips the
/// grab bit `0x10`, the duck `0x1000` and its counter `0x2000`, and the moving attacks (the run attack, the charge, the
/// dive) `0x1000000`, as seen at runtime (docs/research/combat.md#input-return, docs/research/combat.md#block).
/// **Coney choices** where the research names no bits: every attack the dispatcher starts (the walk attack, the snaps,
/// the grounded and mounted strikes, the grab strikes, power strikes and throws) is built as `Attack_Start`'s, and so
/// are rage's start and a theft's clips; the charge and dive hold the run attack's bit; the grab's connecting clips,
/// its spins, the mugging's clips and the let-go hold the grab bit.
inline constexpr HeldFlags kAttackHolds{.held = anim::kFlagAttackPhases, .set = anim::kFlagWindUp};
inline constexpr HeldFlags kGrabHolds{.held = combat::kPhaseGrabStart, .set = combat::kPhaseGrabStart};
inline constexpr HeldFlags kDuckHolds{.held = combat::kPhaseDuck, .set = combat::kPhaseDuck};
inline constexpr HeldFlags kCounterHolds{.held = combat::kPhaseCounter, .set = combat::kPhaseCounter};
inline constexpr HeldFlags kMovingAttackHolds{.held = combat::kPhaseRunAttack, .set = combat::kPhaseRunAttack};

/// The moves of a hold switch both humans on the same update, with no fade (docs/research/combat.md#grab-posing).
inline constexpr float kPairFade = 0.0F;

/// No clips: just a loop.
inline constexpr std::array<std::uint32_t, 0> kNoClips{};

/// The one clip `clip` as an array.
[[nodiscard]] inline std::array<std::uint32_t, 1> one(std::uint32_t clip) { return {clip}; }

/// Anim id `animId` as a clip id.
[[nodiscard]] inline std::uint32_t clipOf(int animId) { return static_cast<std::uint32_t>(animId); }

/// Whether grabber clip `clip` is a spin (78 front to rear, 80 rear to front).
[[nodiscard]] inline bool isSpin(std::uint32_t clip) {
    return clip == clipOf(combat::anim_id::kGrabSpinToRear) || clip == clipOf(combat::anim_id::kGrabSpinToFront);
}

/// Whether grabber clip `clip` is a grab's power move: 57 or 63 and their extensions, + 2 and + 4.
[[nodiscard]] inline bool isPowerMove(std::uint32_t clip) {
    const auto first = clipOf(combat::anim_id::kGrabPower1Strike1);
    const auto rage = clipOf(combat::anim_id::kGrabPower2Strike1);
    return clip == first || clip == first + 2 || clip == first + 4 || clip == rage || clip == rage + 2 ||
           clip == rage + 4;
}

/// Whether `animId` is one of the throws.
[[nodiscard]] inline bool isThrow(int animId) {
    return animId >= combat::anim_id::kThrow1Front && animId <= combat::anim_id::kThrow2Left;
}

} // namespace coney::human::clips
