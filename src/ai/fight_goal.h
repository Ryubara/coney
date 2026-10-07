// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/attack_choice.h"
#include "ai/goal.h"
#include "human/human.h"

// The fight goal (type `0xf`): while it is on top, each update it checks its target, tries a tackle, tries to block an
// attack announced on its human, waits for its actions, picks an attack kind, asks whether it may attack now
// (Brain_CheckAttack), and then holds a ring round the target, walks into the kind's reach, or presses the attack.
// Research: docs/research/ai.md#fight

namespace coney::ai {

/// The fight goal ends when the target is farther than the far melee range × this.
inline constexpr float kFightRangeScale = 1.1F;
/// The move-to-human action's limit into the kind's reach: 2000 ms, 1000 for a class-13 fighter.
inline constexpr int kShortMoveMs = 1000;
inline constexpr int kLongMoveMs = 2000;
/// A target is in reach within this share of the attack's far range: **Coney choice** standing in for `0x00230d00`
/// (not traced): the attack's own target search finds a target at any angle within 0.9 × the far range.
inline constexpr float kInReachShare = 0.9F;

/// The fight goal.
/// @orig 0x002b2c20 FightGoal_Init (unknown)
class FightGoal final : public Goal {
  public:
    /// A fight goal whose deadline is `durationMs` after its start (kNoFightLimit: already past, as `GoalFight`'s).
    explicit FightGoal(int durationMs = kNoFightLimit) : Goal(GoalType::Fight), m_durationMs(durationMs) {}

    /// Sets the deadline (`+0x24`, now + the duration) and the re-target's second.
    void start(Brain& brain) override;
    /// One update, in the original's order (`FightGoal_Process`):
    /// 1. done when the target is not a valid enemy or holds no attack slot for this brain;
    /// 2. the tackle try (tryTackle());
    /// 3. done beyond the far melee range × 1.1 (it never closes a distance: the Melee goal beneath does);
    /// 4. every 30 updates, done without a walkable straight line to the target (the route planner's, when it has one);
    /// 5. once a second, the re-target to the nearest human straight ahead in the sector record;
    /// 6. the block try, giving up the active-attacker place;
    /// 7. while actions are queued, wait; once they end, give up the place;
    /// 8. past the deadline, done unless it still holds an attack slot;
    /// 9. pick a kind when none is chosen (ai::pickAttackFor(), `Human_CanUseAttackKind`);
    /// 10. may it attack now (ai::checkAttack())? If not, hold the reposition ring; out of the kind's reach, move in;
    ///     in reach, the grab and snap try (tryGrab()), the start test (picking again with `Human_CanStartAttack` when
    ///     it refuses), and the press (queueAttack(), with the stick angle the try set).
    ///
    /// **Coney stand-ins**: the fight stance is not built (19 and 20 are the kinds out of it); the rows of
    /// Brain_CheckAttack about held objects (4, 6, 8) never apply, as an AI holds none; the police row applies when
    /// the target's own target is a cop; the reposition is a move to the ring's outer edge, standing within it
    /// (ai-core's band-keeping move replaces it), with no taunt; a threat (the re-target) is a valid enemy on the
    /// brain's list.
    /// @orig 0x002b3ab0 FightGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Gives up the active-attacker place (the goal's suspend, docs/research/ai.md#attack-places).
    void suspend(Brain& brain) override;
    /// Gives up the active-attacker place.
    void end(Brain& brain) override;

    /// The kind chosen and not yet pressed (`+0x10`; kNoAttackKind for none).
    [[nodiscard]] int kind() const { return m_kind; }
    /// Whether it holds an active-attacker place on its target (`+0x28`).
    [[nodiscard]] bool holdsPlace() const { return m_place; }

  private:
    // The tackle try (`FightGoal_TryTackle`): when the tackle meter has passed the gang's threshold, a tackle (a cop's
    // X1 75 % of the time) that can start on a target it may attack, with a place on him; true when it queued it.
    // @orig 0x002b2e28 FightGoal_TryTackle (unknown)
    bool tryTackle(Brain& brain, Brain& target);
    // The grab and snap try (`FightGoal_TryGrab`), once A may attack and is in reach: the snap becomes square with the
    // stick to the side or back; a rear grab when the gang's chance comes up behind T; a man held from behind by a
    // mate is grabbed in front. True when the goal waits this update (the kind may have changed).
    // @orig 0x002b3360 FightGoal_TryGrab (unknown)
    bool tryGrab(Brain& brain, Brain& target);
    // Gives up the active-attacker place on the brain's target.
    void releasePlace(Brain& brain);

    int m_durationMs;
    int m_kind = kNoAttackKind;       // +0x10
    std::optional<float> m_angle;     // the stick angle the grab try set
    std::uint32_t m_updates = 0;      // +0x18
    std::uint64_t m_deadlineMs = 0;   // +0x24
    std::uint64_t m_retargetAtMs = 0; // +0x14
    bool m_place = false;             // +0x28
};

/// Queues attack kind `kind` as its chain of attack actions (ai::chainOf()), each later press delayed by the chain
/// delay of the press before (ai::chainDelayMs() in the human's anims); the first press carries `stickHeading`.
/// @orig 0x0028e248 Brain_QueueAttack (unknown)
void queueAttack(Brain& brain, int kind, std::optional<float> stickHeading = std::nullopt);

/// The distance within which `human` can press attack `kind` at a target: kInReachShare × the far range of the kind's
/// first attack (ai::firstAnimOf()) in its Anim Range List, or of human::kDefaultStrikeReach without one.
[[nodiscard]] float attackReach(const human::Human& human, int kind);

} // namespace coney::ai
