// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"

// How a brain answers its human's events once the gang has passed on them (handler C of `Brain_OnEvent`): its type's
// handler (gang soldiers and Warriors), then the shared one every type falls back on. A hit, a new enemy or an attack
// warning sends a help call: violence (`0x14`) to every human in reach, which a gang soldier or a Warrior answers by
// taking a side, fighting him and walking over (HelpRespondGoal).
// Research: docs/research/ai.md#shared-events, docs/research/ai.md#brain-type-handlers,
// docs/research/ai.md#help-calls, docs/research/ai-goals.md#goal-help-respond

namespace coney::ai {

class Brain;
struct BrainEvent;

/// The help calls' reaches, metres (docs/research/ai.md#help-calls).
inline constexpr float kHelpCallShared = 20.0F; ///< The shared handler's, the Big* goals' and the mugging's.
inline constexpr float kHelpCallBusy = 30.0F;   ///< A busy gang soldier hit by a player.

/// `brain`'s answer to `event` (dead brains are left out by the caller): the type's handler, then the shared one for
/// the ids the type did not take. Returns whether it was used.
/// @orig 0x00304fa8 GangBrain_OnEvent (unknown)
/// @orig 0x003063b0 WarriorBrain_OnEvent (unknown)
/// @orig 0x00292d80 Brain_DefaultOnEvent (unknown)
[[nodiscard]] bool answerEvent(Brain& brain, const BrainEvent& event);

/// A help call by `caller` (`Gang_BroadcastHelpCall`): violence (`0x14`) with `aggressor` as its cause, `caller` as
/// the victim, `value` at `+0x08` and the new-enemy flag, to every other human within `range` of the caller that hears
/// that far (his help hearing range, `+0x138`), players included, up to 60 in the scene's order.
/// @orig 0x00293640 Gang_BroadcastHelpCall (unknown)
/// @orig 0x00293768 AI_AlertNearby (unknown)
/// @orig 0x002935d8 Brain_IsInHearRange (unknown)
void broadcastHelpCall(Brain& caller, float range, Brain* aggressor, int value = 0, bool newEnemy = false);

/// The side `hearer` takes in the violence `event` reports (`Brain_PickSideInFight`): the aggressor (the victim's
/// target when none is named) when he is a threat to the hearer; a stranger hitting the hearer's friend is made one,
/// the gangs made enemies (not by a cop); else the victim when only the aggressor is a friend; else none. **Coney
/// stand-in**: a threat is a man on his enemy list or of an enemy gang (`Brain_IsThreatTo` is not researched).
/// @orig 0x002912b0 Brain_PickSideInFight (unknown)
Brain* pickSideInFight(Brain& hearer, const BrainEvent& event);

/// Violence seen by a gang soldier or a Warrior (`Brain_OnViolenceSeen`): with a threat response, no target, nothing
/// held, free actions and a top goal other than EngageEnemy, he takes a side, fights him (or takes him as the target
/// when already fighting) and, with no HelpRespond goal above his base, walks over to help. **Coney stand-ins**: the
/// Mark goal's noise reaction and a Warrior's look at a busy side are not built; no trains.
/// @orig 0x00291960 Brain_OnViolenceSeen (unknown)
void onViolenceSeen(Brain& brain, const BrainEvent& event);

/// The gangs of `brain` and `other` made enemies, unless they are already (`Brain_MakeGangsEnemies`). **Coney
/// choice**: nothing for friends or a human with no gang.
/// @orig 0x00290328 Brain_MakeGangsEnemies (unknown)
void makeGangsEnemies(const Brain& brain, const Brain& other);

/// The help-respond goal (type `0x1d`, `Gang_SendHelper` pushes it on the hearer himself): at Start, a target within
/// 1.1 × far with a walkable line is turned to and watched for 3 s; otherwise he goes to him (gait 2, 4 beyond 10 m
/// or with no enemies listed). Every 30 updates it ends once the brain lists enemies (a fight takes over); it ends
/// when its actions are done. **Coney stand-ins**: the gang's wanted timer is not refreshed; the 60 % look-around (goal
/// 10, clip `0x29e`) before going to a near target with no walkable line is not built; level `0x53`'s gait 5 is not
/// kept.
/// @orig 0x002b7670 HelpRespondGoal_Init (unknown)
class HelpRespondGoal final : public Goal {
  public:
    /// Helping against `target` (its `+0x14` is always 0: listed enemies end it).
    explicit HelpRespondGoal(Brain& target) : Goal(GoalType::HelpRespond), m_target(&target) {}

    /// @orig 0x002b76a0 HelpRespondGoal_Start (unknown)
    void start(Brain& brain) override;
    /// @orig 0x002b7900 HelpRespondGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Clears the actions.
    /// @orig 0x002b78e0 HelpRespondGoal_End (unknown)
    void end(Brain& brain) override;

    /// The human he helps against (`+0x10`).
    [[nodiscard]] const Brain* target() const { return m_target; }

  private:
    Brain* m_target;
    int m_updates = 0;
    std::uint64_t m_lookUntilMs = 0; // the look at a near target
};

/// `Gang_SendHelper(brain, side, 0, −1)`: a HelpRespond goal against `side` pushed on `brain` himself.
/// @orig 0x002b75b8 Gang_SendHelper (unknown)
void sendHelper(Brain& brain, Brain& side);

} // namespace coney::ai
