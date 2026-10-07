// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace coney::combat {
class AnimRangeList;
} // namespace coney::combat

// How close an AI must be to an attack's target, per attack kind: the near reach the fight goal walks into
// (`Attack_ReachSquared`) and the far reach `Human_CanStartAttack` refuses beyond (`Attack_FarReachSquared`). Each is
// one anim's record in the attacker's Anim Range List (its reach `+0x4`, or its far range), or a fixed distance for
// throws, ranged objects and kinds with no clip. Pure: the caller says what A holds and how he moves.
// Research: docs/research/ai.md#attack-reach, docs/research/combat.md#ai-attacks

namespace coney::ai {

/// A reach with no clip of its own (kinds 18, 24-31, 33-44 empty-handed, and anything the tables leave out), m.
inline constexpr float kNoClipReach = 2.0F;
/// The throw's reach (kind 23, and kinds 0 and 1 with a throwable beyond the near melee range), m.
inline constexpr float kThrowReach = 12.0F;
/// Kinds 0-9 with a held object of anim set 4 or 5, m.
inline constexpr float kSet4Reach = 15.0F;
inline constexpr float kSet5Reach = 30.0F;

/// What the reach of a kind reads of the attacker.
struct ReachInput {
    int kind = 0;
    bool running = false;         ///< Gait 4 or 5 (kind 0 is the run attack 24).
    bool walking = false;         ///< Moving below the run (kind 0 is the walk attack 23).
    bool targetDown = false;      ///< A's target is knocked down (kind 0's near reach is the grounded strike 194).
    int heldSet = 0;              ///< The held object's anim set (0 empty-handed).
    bool dealer = false;          ///< Brain type 5: a throwable is never thrown from range.
    bool beyondNearRange = false; ///< The target stands beyond the near melee range.
};

/// Where a reach comes from: one anim's record, or a fixed distance.
struct ReachSource {
    std::optional<std::uint32_t> anim; ///< The anim whose record gives it; none for a fixed distance.
    float metres = kNoClipReach;       ///< The fixed distance when there is no anim.
};

/// The near reach's source (`Attack_ReachSquared`, the fight goal's walk-in): kind 0's run, walk, grounded or X1 clip;
/// 1, 3 and 5-9 S1 (12); 2 and 4 X1 (11); the specials' own clips; the grab 72; the throw 12 m; 2 m for the rest. A
/// held object changes kinds 0-9, 18, 24-31 and 33-44 by its set.
/// @orig 0x00230d00 Attack_ReachSquared (unknown)
[[nodiscard]] ReachSource nearReachSource(const ReachInput& input);

/// The far reach's source (`Attack_FarReachSquared`, `Human_CanStartAttack`'s range test): as the near one, but kinds
/// 1-9 take their own clips (12-20), kind 0 has no grounded case and the grab is 70.
/// @orig 0x00230930 Attack_FarReachSquared (unknown)
[[nodiscard]] ReachSource farReachSource(const ReachInput& input);

/// The near reach in metres: the anim's reach (`AttackTable_GetReach`, record `+0x4`, as is). **Coney stand-in**: an
/// anim with no record, or no list, reaches human::kDefaultStrikeReach.
/// @orig 0x002544a0 AttackTable_GetReach (unknown)
[[nodiscard]] float nearReach(const ReachSource& source, const combat::AnimRangeList* ranges);

/// The far reach in metres: the anim's far range (`AttackTable_GetFarRange`). **Coney stand-in** as nearReach().
/// @orig 0x00254508 AttackTable_GetFarRange (unknown)
[[nodiscard]] float farReach(const ReachSource& source, const combat::AnimRangeList* ranges);

} // namespace coney::ai
