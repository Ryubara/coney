// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"

// Two goals the scripts push on one human against another (`GoalMoveToHuman`, `GoalEngageEnemy`): run to a human and
// stop near him, or walk up to an enemy and fight him. Survival's spawned police are sent at the players with them.
// Research: docs/references/bindings/ai.md#goalmovetohuman, docs/references/bindings/ai.md#goalengageenemy

namespace coney::ai {

class ScriptServices;

/// How often the move-to-human goal drops its move and heads for where its target is now, ms.
inline constexpr std::uint32_t kMoveToHumanReissueMs = 1000;
/// Updates the move-to-human goal waits after a failed route before trying again.
inline constexpr int kMoveToHumanRetryUpdates = 30;
/// How long one run of the engage goal at its enemy lasts before it looks again, ms (**Coney choice**, as the hold-flag
/// goal's run: the page does not give the engage goal's).
inline constexpr std::uint32_t kEngageRunMs = 1000;

/// The move-to-human goal (type 6). Each update: done when the human is down, when the target no longer names a human
/// in the world that is alive, or once within `radius` of the target (in 3D); otherwise every kMoveToHumanReissueMs
/// it drops its move and moves to where the target is now at `gait`. After a failed route it waits
/// kMoveToHumanRetryUpdates updates before trying again. **Coney stand-ins**: what `0x0028d4b0` accepts as a valid
/// target is taken as alive and in the world, and the fight stance it leaves is not built.
/// @orig 0x002dc4f8 MoveToHumanGoal_Init (unknown)
/// @orig 0x002dc578 MoveToHumanGoal_Process (unknown)
class MoveToHumanGoal final : public Goal {
  public:
    /// Going to the human with handle `target`, found through `services` (which must outlive it).
    MoveToHumanGoal(ScriptServices& services, double target, int gait, float radius)
        : Goal(GoalType::MoveToHuman), m_services(&services), m_target(target), m_gait(gait), m_radius(radius) {}

    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The target's handle, the gait and the arrival radius.
    [[nodiscard]] double target() const { return m_target; }
    [[nodiscard]] int gait() const { return m_gait; }
    [[nodiscard]] float radius() const { return m_radius; }

  private:
    ScriptServices* m_services;
    double m_target;                // +0x20
    int m_gait;                     // +0x24
    float m_radius;                 // +0x28
    std::uint64_t m_nextMoveMs = 0; // when the move is next dropped and re-issued
    int m_failedUpdates = 0;        // updates since a failed route
};

/// The engage-enemy goal (type 11). Each update: done when the enemy no longer names a human in the world who can
/// fight; waits while its human is down or busy with actions; otherwise takes the enemy as its target and enemy, runs
/// at him beyond the reach of an attack (MoveToHumanAction, kEngageRunMs at a time) and within it pushes the fight
/// goal.
/// **Coney stand-ins**: the range beyond which the original gives up is not on the page, so it follows the enemy
/// anywhere; the flag at `+0x34` is not built.
/// @orig 0x002af5b0 EngageEnemyGoal_Init (unknown)
/// @orig 0x002afa48 EngageEnemyGoal_Process (unknown)
class EngageEnemyGoal final : public Goal {
  public:
    /// Fighting the human with handle `enemy`, found through `services` (which must outlive it).
    EngageEnemyGoal(ScriptServices& services, double enemy)
        : Goal(GoalType::EngageEnemy), m_services(&services), m_enemy(enemy) {}

    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The enemy's handle.
    [[nodiscard]] double enemy() const { return m_enemy; }

  private:
    ScriptServices* m_services;
    double m_enemy;
};

} // namespace coney::ai
