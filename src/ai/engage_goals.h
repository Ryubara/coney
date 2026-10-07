// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/attack_choice.h"
#include "ai/goal.h"

// Two goals that take one human to another: the move-to-human goal (`GoalMoveToHuman`), which runs to a human and stops
// near him, and the engage-enemy goal, the run-in of a fight, which the Melee goal pushes at a distant target and the
// scripts push with `GoalEngageEnemy`. Survival's spawned police are sent at the players with them.
// Research: docs/research/ai.md#engage-enemy, docs/references/bindings/ai.md#goalmovetohuman,
// docs/references/bindings/ai.md#goalengageenemy

namespace coney::ai {

class ScriptServices;

/// How often the move-to-human goal drops its move and heads for where its target is now, ms.
inline constexpr std::uint32_t kMoveToHumanReissueMs = 1000;
/// Updates the move-to-human goal waits after a failed route before trying again.
inline constexpr int kMoveToHumanRetryUpdates = 30;

/// The engage goal's numbers (docs/research/ai.md#engage-enemy): the charge's range (`+0x30`, 2.56 squared), the stop's
/// share of the far melee range, the give-up distance, the re-plan and re-target periods, the move's radius, the fan
/// per attack slot, and how near a new target must be to be taken (`Human_CanSeeHuman`'s 9 m).
inline constexpr float kChargeRange = 1.6F;
inline constexpr float kEngageStopShare = 0.75F;
inline constexpr float kEngageGiveUpRange = 20.0F;
/// The runner sprints after a running target only with more stamina than this, percent (`0x00222fa0`).
inline constexpr int kEngageSprintStamina = 50;
inline constexpr std::uint64_t kEngageReplanMs = 250;
inline constexpr std::uint64_t kEngageRetargetMs = 2000;
inline constexpr float kEngageMoveRadius = 0.5F;
inline constexpr float kEngageFanDegrees = 9.0F;
inline constexpr float kEngageSeeRange = 9.0F;

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

/// The engage-enemy goal (type 11): the run-in (docs/research/ai.md#engage-enemy). The Melee goal pushes it at a target
/// beyond the fight goal's range; it runs at him (gait 4, 5 when he runs), re-planning every 250 ms, and ends, handing
/// back to the Melee goal, by stopping and turning to him: within 0.75 × the far melee range when he is busy, or when
/// the charge is not armed and he walks or stands; or after a failed move. Armed (he was at least the far range away at
/// its start, or he runs), it attacks out of the run within 1.6 m: a cop's X1 (50 %, 80 % at a gang member) or tackle,
/// anyone else's pick (ai::pickAttackFor()), which ends the run-in when it is not a charge kind; it presses once the
/// kind can start, as one attack action (the X1 along the runner's heading); another human the near one in the sector
/// of the target's record the runner comes from disarms the charge (the runner himself there waits). It gives up only
/// far off and out of sight, and has no time limit. It clears the target's `+0x1ec`, so the fight may attack him at
/// once. The re-target takes the nearest enemy it can see within 9 m; out of sight is the line of sight
/// (ai::lineOfSight()); it sprints only with more than half its stamina. **Coney stand-ins**: the target is busy while
/// not standing or while his record holds an attack's flags (kAttackWaitFlags); the lead applies while the runner
/// stands in his sectors 3-5, fanned 9° per attack slot index; "actions blocked" ends nothing (a human down waits); the
/// shouts, the taunt and brain `+0x0b` are not built. The binding's goal (`GoalEngageEnemy`, by handle) takes the enemy
/// as its enemy and target, and where the fight's goal would end it pushes a fight goal and goes on (the wrapper
/// `0x002af528` is not traced; its page says the human fights the enemy until he is out of range or gone).
/// @orig 0x002af5b0 EngageEnemyGoal_Init (unknown)
class EngageEnemyGoal final : public Goal {
  public:
    /// The fight's run-in at the brain's target (the Melee goal's).
    EngageEnemyGoal() : Goal(GoalType::EngageEnemy) {}
    /// `GoalEngageEnemy`'s: at the human with handle `enemy`, found through `services` (which must outlive it).
    EngageEnemyGoal(ScriptServices& services, double enemy)
        : Goal(GoalType::EngageEnemy), m_services(&services), m_enemy(enemy) {}

    /// Arms the charge when the target is at least the far melee range away; notes a slow start (below a jog); raises
    /// the turn boost by one, so the run-in turns faster.
    /// @orig 0x002af670 EngageEnemyGoal_Start (unknown)
    void start(Brain& brain) override;
    /// One update, as above.
    /// @orig 0x002afa48 EngageEnemyGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Clears the actions and lowers the turn boost again.
    /// @orig 0x002af8d0 EngageEnemyGoal_End (unknown)
    void end(Brain& brain) override;

    /// The enemy's handle (the binding's goal; 0 for the fight's).
    [[nodiscard]] double enemy() const { return m_enemy; }
    /// Sets the run-in distance (`+0x30`, kChargeRange by default): a boss's BigBrawler gives 2.5 m.
    void setRunIn(float metres) { m_runIn = metres; }
    /// Whether the charge is armed (`+0x35`).
    [[nodiscard]] bool chargeArmed() const { return m_charge; }

  private:
    // The target: the binding's enemy, or the brain's.
    Brain* targetOf(Brain& brain) const;
    // Begins the stop: the move ends, and the goal waits until the human stands.
    GoalStatus beginStop(Brain& brain);
    // Where the stop ends: the fight's goal is done; the binding's pushes a fight goal and goes on.
    GoalStatus arrive(Brain& brain, const Brain& target);

    ScriptServices* m_services = nullptr;
    double m_enemy = 0;
    float m_runIn = kChargeRange;     // +0x30 (as a distance)
    bool m_charge = false;            // +0x35
    int m_chargeKind = kNoAttackKind; // +0x20, the charge's kind
    bool m_boosted = false;           // Start raised the turn boost
    bool m_startSlowly = false;       // +0x34
    bool m_stopping = false;          // +0x39
    bool m_moved = false;             // a move has been queued
    std::uint64_t m_nextPlanMs = 0;
    std::uint64_t m_nextRetargetMs = 0; // +0x1c
    float m_planHeading = 0.0F;         // the target's heading at the last plan
    bool m_planRunning = false;         // whether the target ran at the last plan
};

} // namespace coney::ai
