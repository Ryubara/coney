// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "human/face.h"

// The mood a brain gives its human's face (human/face.h), from what it is doing: the original's
// `Brain_UpdateAlertness`, run every fifth update by the player's brain and the AI brains alike.
// Research: docs/research/face.md

namespace coney::ai {

class Brain;

/// Updates between two of a brain's face moods.
inline constexpr std::uint64_t kFaceMoodUpdates = 5;

/// The facts the mood is picked from, read off a brain (faceMoodFacts()) or written by a test.
struct FaceMoodFacts {
    bool down = false;         ///< Down or dead (`Human_IsDownOrDead`).
    bool cuffed = false;       ///< Cuffed (`Human_IsCuffed`).
    bool onFire = false;       ///< Burning (`Human_IsOnFire`).
    bool gangTactic = false;   ///< Its gang has a tactic (gang `+0x40`).
    bool tacticAlerts = false; ///< That tactic's vtable `+0x3c` flag (Tactic::alertsGang()).
    int tacticType = -1;       ///< That tactic's type id (vtable `+0x34`).
    bool padDriven = false;    ///< A pad drives the human (per-player record `+0x1b`).
    bool fighting = false;     ///< A fight goal on the stack (`Brain_HasFightGoal`: Melee or LeftTurf).
    bool chasing = false;      ///< A chase goal on the stack (`Brain_HasChaseGoal`: `0x0c` or LeftTurf `0x3f`).
    int topGoal = -1;          ///< The top goal's type id, -1 for none.
};

/// The mood for `facts`: scared when down, cuffed or burning; with a gang tactic, angry under an alerting one, else by
/// its type (scared under types `0x13`, `0x20` and `0x25` unless a pad drives the human, excited under `0x18`, `0x1b`
/// and `0x24`, angry under `0x14`, `0x15`, `0x1c`-`0x1e`, `0x22`, `0x23` and `0x26`, neutral otherwise); without one,
/// angry while fighting or chasing, else neutral, and then the top goal's type may decide (excited for bums,
/// peddlers, rioters, dealers and four more; angry for a run-carrot, a hostile, a devil run and four more; scared for
/// a fleeing pedestrian, a lead chase, a gang call and six more).
/// @orig 0x0028bbf0 Brain_UpdateAlertness (unknown)
[[nodiscard]] human::FaceMood faceMoodOf(const FaceMoodFacts& facts);

/// The facts of `brain` now: the queries Brain_UpdateAlertness makes before it picks. **Coney stand-in**: `onFire` is
/// false (Coney has no burning humans yet).
/// @orig 0x0028bbf0 Brain_UpdateAlertness (unknown)
[[nodiscard]] FaceMoodFacts faceMoodFacts(Brain& brain);

} // namespace coney::ai
