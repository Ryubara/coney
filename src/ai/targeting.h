// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// How an AI chooses whom to fight among its enemies: which may be chosen at all (Brain_ValidateEnemy), whether one may
// be chased and attacked now, the score of each (Brain_ScoreEnemy, its weights set by `CfgSetTargetingPoints` and
// `CfgSetTargetingPointsEx`), and the pick of the best (Brain_PickBestEnemy). Research:
// docs/research/ai.md#enemy-score, docs/research/ai.md#sight

namespace coney::ai {

class Brain;
class Goal;

/// The score weights (`0x00510b4c`-`0x00510ba4`), as `config_preload2.lua` sets them; the executable's own defaults
/// differ for four (distance 3, enemy leads 3, targets me 1, a player 3). Those without a script setter keep the
/// executable's values.
struct TargetingPoints {
    float perMetre = 4.0F;       ///< `0x1`: per metre the enemy is inside the sight range (`0x00510b4c`).
    float leaderPerMetre = 3.0F; ///< `0x2`: per metre he is inside the sight range of the scorer gang's leader.
    int enemyLeads = 6;          ///< `0x4`: he leads his gang (not for a Warrior scorer).
    int stunned = 1;             ///< `0x8`.
    int outOfView = -5;          ///< `0x10`.
    int down = 1;                ///< `0x20`.
    int grabbed = 1;             ///< `0x40`.
    int grabbedFromRear = 6;     ///< `0x80`.
    int targetsMe = 4;           ///< `0x200`.
    int running = 3;             ///< `0x400`.
    int nearTrain = 6;           ///< `0x800` (never counts: such an enemy is ruled out).
    int tagging = 3;             ///< `0x1000`.
    int targetsMeArmed = 3;      ///< `0x2000`.
    int player = 20;             ///< `0x4000`.
    int arrested = -10;          ///< `0x8000` (no setter).
    int police = 30;             ///< `0x10000` (no setter).
    int noWalkableLine = -5;     ///< Always: no walkable straight line to him (`0x00510b90`).
    int cannotChase = -5;        ///< The base when the scorer cannot chase him (`0x00510b94`).
    int notAttackable = -10;     ///< Always: he may not be attacked now (no setter).
    int gangTarget = 400;        ///< Always: he is the scorer gang's chosen target (no setter).
    int previousTarget = 3;      ///< The default goal adjustment: the previous target (`0x00510b84`).
};

/// The score terms a caller asks for (Brain_ScoreEnemy's flags).
namespace score_term {
inline constexpr std::uint32_t kDistance = 0x1;
inline constexpr std::uint32_t kNearLeader = 0x2;
inline constexpr std::uint32_t kEnemyLeads = 0x4;
inline constexpr std::uint32_t kStunned = 0x8;
inline constexpr std::uint32_t kOutOfView = 0x10;
inline constexpr std::uint32_t kDown = 0x20;
inline constexpr std::uint32_t kGrabbed = 0x40;
inline constexpr std::uint32_t kGrabbedFromRear = 0x80;
inline constexpr std::uint32_t kTargetsMe = 0x200;
inline constexpr std::uint32_t kRunning = 0x400;
inline constexpr std::uint32_t kNearTrain = 0x800;
inline constexpr std::uint32_t kTagging = 0x1000;
inline constexpr std::uint32_t kTargetsMeArmed = 0x2000;
inline constexpr std::uint32_t kPlayer = 0x4000;
inline constexpr std::uint32_t kArrested = 0x8000;
inline constexpr std::uint32_t kPolice = 0x10000;
/// The Melee goal's terms: every one but kNearLeader (`0xfffffffd`).
inline constexpr std::uint32_t kMelee = 0xfffffffdU;
} // namespace score_term

/// The score of an enemy that may not be taken.
inline constexpr float kRuledOut = -999.0F;
/// A gang of this kind (the police) sees at least kPoliceSightRange.
inline constexpr int kPoliceGangKind = 1;
inline constexpr float kPoliceSightRange = 30.0F;

/// The sight range a brain scores and spots by (`Brain_GetSightRange`): its own (`+0x130`), at least 30 m in a police
/// gang.
/// @orig 0x0028bf00 Brain_GetSightRange (unknown)
[[nodiscard]] float sightRangeOf(const Brain& brain);

/// Whether `human` may be chosen as `scorer`'s enemy (`Brain_ValidateEnemy`): in the world with health left, not on
/// the ground knocked out, and targetable (brain `+0x120`, `GangSetTargetable` / `HuSetNoTarget`). **Coney stand-ins**:
/// Coney keeps no mugging, arrest, interrogation or turf-only state, so those tests pass.
/// @orig 0x0028d358 Brain_ValidateEnemy (unknown)
[[nodiscard]] bool validEnemy(const Brain& scorer, const Brain& human);

/// Whether `target` can be chased (`Brain_CanBeChased`): its brain's reachable byte (`+0x11e`, `HuMarkReachable`).
/// **Coney stand-in**: no trains run and no human burns, so those tests pass.
/// @orig 0x0028abc0 Brain_CanBeChased (unknown)
[[nodiscard]] bool canBeChased(const Brain& target);

/// Whether `target` may be attacked now by `attacker` (null for anyone; `Brain_IsAttackableBy`): its attackable byte
/// (`+0x11f`, `GangSetAttackable`), with two exceptions: a civilian (type 4) of a gang of kind `0x17` attacked by a
/// Warrior (type 3) whom he does not target himself, while he has no target or an AI one, only when he is the Warrior
/// gang's chosen target; and a human of class 221 (the dog) whose byte is clear stays attackable (**Coney choice**:
/// Coney builds no GrabTarget goal on the attacker that would refuse it).
/// @orig 0x00290ea8 Brain_IsAttackableBy (unknown)
[[nodiscard]] bool attackableBy(const Brain& target, const Brain* attacker);

/// Whether `scorer` may take an attack slot on `enemy` (`Brain_CanTakeSlotOn`): his slots are not all taken, or the
/// scorer holds one, or a current holder is farther from him than the scorer.
/// @orig 0x0028dc80 Brain_CanTakeSlotOn (unknown)
[[nodiscard]] bool canTakeSlotOn(const Brain& scorer, const Brain& enemy);

/// `scorer`'s score for `enemy` with the terms `flags` (`Brain_ScoreEnemy`): kRuledOut for one it may not take a slot
/// on, or beyond its sight range when kDistance is asked; otherwise the sum of the asked terms and the four that
/// always count (docs/research/ai.md#enemy-score), by the scorer's TargetingPoints.
/// @orig 0x0029ce98 Brain_ScoreEnemy (unknown)
[[nodiscard]] float scoreEnemy(const Brain& scorer, const Brain& enemy, std::uint32_t flags);

/// Picks `brain`'s target among its enemies (`Brain_PickBestEnemy`): the target is cleared; each valid enemy is
/// scored, then adjusted by `goal` (Goal::adjustEnemyScore(), the previous target's bonus by default; null applies
/// the default); the best above 0 wins (the first of equal ones), the walk stopping after every five enemies once there
/// is a winner. The winner (or none) becomes the target. Returns it.
/// @orig 0x0029f230 Brain_PickBestEnemy (unknown)
Brain* pickBestEnemy(Brain& brain, const Goal* goal, std::uint32_t flags);

} // namespace coney::ai
