// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "combat/anim_ids.h"

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
inline constexpr std::uint32_t kBlockShuffle = 607;

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

/// Whether `animId` is one of the throws.
[[nodiscard]] inline bool isThrow(int animId) {
    return animId >= combat::anim_id::kThrow1Front && animId <= combat::anim_id::kThrow2Left;
}

} // namespace coney::human::clips
