// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The anim ids combat starts or reads, by their names in the original's enum (docs/references/anim-ids.md). An anim id
// indexes a character's clips and its Anim Range List, so these also name each move's damage.
// Research: docs/research/combat.md#damage-table

namespace coney::combat::anim_id {

inline constexpr int kNone = -1; ///< No anim.

inline constexpr int kRunningAttackCharge = 0; ///< L2 + cross at a run or a sprint.
inline constexpr int kRunningAttackDive = 1;   ///< L2 + square at a run or a sprint.
inline constexpr int kTackleMiss = 2;          ///< `TACKLE_MISS`.
inline constexpr int kTackleIntro = 3;         ///< The tackle's range record.
inline constexpr int kTacklePlayerIntro = 4;   ///< A tackle starts.

inline constexpr int kAttackX1 = 11;
inline constexpr int kAttackS1 = 12;
inline constexpr int kAttackXX2 = 13;
inline constexpr int kAttackXS2 = 14;
inline constexpr int kAttackSX2 = 15;
inline constexpr int kAttackSS2 = 16;
inline constexpr int kAttackSSX3 = 17;
inline constexpr int kAttackSSS3 = 19;
inline constexpr int kAttackFromWalk = 23;
inline constexpr int kAttackFromRun = 24;
inline constexpr int kSnapRight = 25;
inline constexpr int kSnapLeft = 27;
inline constexpr int kSnapBack = 29;

inline constexpr int kGrabComboStrike1 = 51;
inline constexpr int kGrabComboStrike2 = 53;
inline constexpr int kGrabComboStrike3 = 55;
inline constexpr int kGrabPower1Strike1 = 57; ///< The power strike.
inline constexpr int kGrabPower2Strike1 = 63; ///< The power strike in rage.
inline constexpr int kGrabMiss = 69;          ///< `GRAB_MISS`.
inline constexpr int kGrabIntro = 70;         ///< The grab's range record.
inline constexpr int kGrabPlayerIntro = 71;   ///< A grab starts.
inline constexpr int kGrabSpinToRear = 78;    ///< The spin from a front hold to a rear one (victim 79).
inline constexpr int kGrabSpinToFront = 80;   ///< The spin from a rear hold to a front one (victim 81).
inline constexpr int kGrabLetGo = 95;         ///< The player lets go of a grab (victim 94).
inline constexpr int kGrabFrontStrike = 120;  ///< Square at a grabbed target.

inline constexpr int kThrow1Front = 147;
inline constexpr int kThrow1Right = 149;
inline constexpr int kThrow1Rear = 151;
inline constexpr int kThrow1Left = 153;
inline constexpr int kThrow2Front = 155; ///< The THROW_02 set: a wall within reach.
inline constexpr int kThrow2Right = 157;
inline constexpr int kThrow2Rear = 159;
inline constexpr int kThrow2Left = 161;

inline constexpr int kGroundedStrike1 = 193;
inline constexpr int kGroundedStrike2 = 194;
inline constexpr int kMountingStrike = 212;

inline constexpr int kRageStart = 643;
inline constexpr int kBreakObjectLow = 661;
inline constexpr int kBreakObjectMid = 662;
inline constexpr int kStereoStealEnd = 685;
inline constexpr int kStereoStealFail = 686;

} // namespace coney::combat::anim_id
