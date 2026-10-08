// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/goal.h"
#include "ai/tactic.h"
#include "animation/anim_math.h"

// A player's crew under the Warrior commands' default: the follow tactic the game issues itself (when the chief is
// made a player and when a scene ends), the follow goal it gives each member (keep to the chief's formation, face his
// enemies, never attack), the attack and hold commands' tactics and goals, and the chief's automatic commands that
// switch the crew to attack or defend and back. Research: docs/research/ai.md#warrior-commands,
// docs/research/ai.md#warrior-follow, docs/research/ai.md#warrior-attack, docs/research/ai.md#warrior-hold,
// docs/research/ai-goals.md#goal-follow-and-attack, docs/research/ai-goals.md#goal-hold-position,
// docs/research/ai.md#warrior-auto-commands

namespace coney::ai {

class Formations;
class ScriptServices;

/// The Warrior commands by number (docs/references/commands.md#warrior-command).
inline constexpr int kCrewFollow = 0;
inline constexpr int kCrewAttack = 1;
inline constexpr int kCrewDefend = 2;
inline constexpr int kCrewHold = 3;
inline constexpr int kCrewSteal = 5;

/// The follow tactic's distance (`0x00310e00` is given 9.0) and the formation slot reach its follow goal walks to.
inline constexpr float kCrewFollowDistance = 9.0F;
inline constexpr float kCrewSlotReach = 0.75F;
/// The follow goal re-takes its slot every this many updates.
inline constexpr std::uint64_t kCrewSlotPeriod = 31;
/// In fight mode it takes the nearest attacker of the crew within this distance, metres.
inline constexpr float kCrewAttackerReach = 20.0F;
/// Out of fight mode (mode 3) it turns to the leader's heading when more than this off it (radians, 60°), every 2-4 s.
inline constexpr float kCrewHeadingTurn = 1.0471976F;
inline constexpr int kCrewTurnMinMs = 2000;
inline constexpr int kCrewTurnMaxMs = 4000;
/// Its move to the chief's enemy is cut after this long.
inline constexpr std::uint32_t kCrewMoveMs = 3000;

/// `GoalFollowPlayer(distance, leader, mode)` (type `0x32`, vtable `0x00541d70`): keeps to the leader's formation. Out
/// of fight mode it walks back to its slot every 31 updates (FollowFormationGoal) and, in mode 3, turns to the
/// leader's heading when more than 60° off it every 2-4 s. In fight mode (a member of its gang has attackers and it
/// is someone's enemy) it keeps a target, else takes the nearest attacker of the gang within 20 m or the leader's
/// target, and turns to it within the far melee range or moves to it (3 s); it never attacks.
/// **Coney stand-ins**: the straight-line test that also sends it back to its slot, the fight stance, the 2π field of
/// view, fetching pick-ups and the idle actions are not built; "someone's enemy" (`+0x152`, inferred) is read as any
/// fightable brain holding it among its enemies.
/// @orig 0x002de380 Goal_FollowPlayer (unknown)
class FollowPlayerGoal final : public Goal {
  public:
    /// Follows the human with handle `leader`, found through `services`, in `formations` (both must outlive it).
    FollowPlayerGoal(ScriptServices& services, Formations& formations, double leader, float distance, int mode)
        : Goal(GoalType::FollowPlayer), m_services(&services), m_formations(&formations), m_leader(leader),
          m_distance(distance), m_mode(mode) {}

    /// Joins the leader's formation (made on first use).
    void start(Brain& brain) override;
    /// Done when the leader is gone; else the fight mode's turn or move, or the slot walk and the turn to his heading.
    /// @orig 0x002de7c0 FollowPlayerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Leaves the formation.
    void end(Brain& brain) override;

    /// The leader's handle, the distance and the mode.
    [[nodiscard]] double leader() const { return m_leader; }
    [[nodiscard]] float distance() const { return m_distance; }
    [[nodiscard]] int mode() const { return m_mode; }

  private:
    // Whether it fights for the leader this update (`0x002de650`).
    [[nodiscard]] bool fightMode(const Brain& brain) const;
    // The fight mode's update: a target, then the turn or the move to it.
    void fight(Brain& brain, Brain& leader);

    ScriptServices* m_services;
    Formations* m_formations;
    double m_leader;
    float m_distance;
    int m_mode;
    std::uint64_t m_nextTurnMs = 0;
};

/// `FollowFormation(reach, leader)` (type `0x33`): walks to its slot in the leader's formation and is done within
/// `reach` of it, without a slot, or once the walk failed. **Coney stand-in**: the walk runs (gait 4) while the slot is
/// more than 3 m away and walks (gait 2) nearer (the gait is not on the page).
/// @orig 0x002dfba8 Goal_FollowFormation (unknown)
class FollowFormationGoal final : public Goal {
  public:
    /// Walks to within `reach` of the slot the follower holds.
    explicit FollowFormationGoal(float reach) : Goal(GoalType::FollowFormation), m_reach(reach) {}
    [[nodiscard]] GoalStatus process(Brain& brain) override;

  private:
    float m_reach;
    bool m_walked = false;
};

/// WarriorFollowTactic (command 0, type `0x12`, vtable `0x005438c0`): the chief's formation takes nine slots and every
/// member that is not the chief, not a player and not down is flushed and given `GoalFollowPlayer(9 m, chief, 3)`. It
/// never ends by itself; event 19 for a member with a goal gives it the follow goal again (mode 1), event 22 with
/// argument 1 gives every member its goal again. **Coney stand-ins**: the second player's defenders, the idle clips of
/// anim group 604, the formation reshuffle every 8 s, banter and the warning and taunt lines are not built.
/// @orig 0x00310e00 WarriorFollowTactic_Create (unknown)
/// @orig 0x00310f38 WarriorFollowTactic_GiveGoals (unknown)
class WarriorFollowTactic final : public Tactic {
  public:
    /// The tactic for `chief`'s crew, with the level's brains and formations (both must outlive it).
    WarriorFollowTactic(double chief, ScriptServices& services, Formations& formations);
    /// Gives the members their goals.
    /// @orig 0x003111a8 WarriorFollowTactic_Start (unknown)
    void start(Gang& gang) override;
    /// Done (1) when the gang has no leader; otherwise 0.
    /// @orig 0x00311638 WarriorFollowTactic_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// Events 19 and 22 (above).
    /// @orig 0x00311928 WarriorFollowTactic_Event (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

    /// The chief's handle.
    [[nodiscard]] double chief() const { return m_chief; }

  private:
    // Gives `member` the follow goal in `mode`, flushed first.
    void follow(Brain& member, int mode);

    double m_chief;
    ScriptServices* m_services;
    Formations* m_formations;
};

/// The attack goal looks for enemies within this distance of the chief, metres.
inline constexpr float kCrewAttackReach = 60.0F;
/// Within this distance of the chief an attack goal with no enemy stays where it is, metres.
inline constexpr float kCrewAttackStay = 8.0F;

/// `FollowAndAttack(chief)` (type `0x34`, vtable `0x005405d0`): what each Warrior runs under the attack command. It
/// joins the chief's formation; each update it fights the best enemy within 60 m of the chief: a fight goal (4000 ms)
/// once in reach, else the fight's run-in (EngageEnemyGoal) while it may close on him, else a straight move to him.
/// With no enemy it stays within 8 m of the chief, turning to his heading every 4 s, and farther it walks back to its
/// formation slot (FollowFormationGoal, 0.75 m). **Coney stand-ins**: picking up things to fight with, the fight
/// stance and its shuffle, the fidgets and the FollowFormation time limit (2000 ms) are not built; its enemies are its
/// own plus (as the follow goal's fight mode) the nearest attacker of the crew; "in reach" is within the far melee
/// range x 1.1 and "chasable" Brain::mayApproach().
/// @orig 0x002bbdc0 FollowAndAttackGoal_Init (unknown)
class FollowAndAttackGoal final : public Goal {
  public:
    /// Fights for the human with handle `leader`, found through `services`, in `formations` (both must outlive it).
    FollowAndAttackGoal(ScriptServices& services, Formations& formations, double leader)
        : Goal(GoalType::FollowAndAttack), m_services(&services), m_formations(&formations), m_leader(leader) {}

    /// Joins the leader's formation (made on first use).
    /// @orig 0x002bbe00 FollowAndAttackGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Done when the leader is gone; else the fight or the stay with the leader (above).
    /// @orig 0x002bc198 FollowAndAttackGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Leaves the formation and stops the move.
    /// @orig 0x002bbe98 FollowAndAttackGoal_End (unknown)
    void end(Brain& brain) override;

    /// The leader's handle.
    [[nodiscard]] double leader() const { return m_leader; }

  private:
    // Its enemy this update: the best of its enemies within kCrewAttackReach of `leader`; null for none.
    Brain* enemy(Brain& brain, Brain& leader);

    ScriptServices* m_services;
    Formations* m_formations;
    double m_leader;
    Brain* m_lastTarget = nullptr;
    std::uint64_t m_nextTurnMs = 0;
};

/// The attack command's tactic (command 1, type 1, vtable `0x00544160`): every member that is not the chief, not a
/// player and not down is flushed and given `FollowAndAttack` on the chief. It never ends by itself. Its type is below
/// `0x12`, so the members keep their own goals. **Coney stand-ins**: dogs (class 221) get the attack goal too, not
/// `AvoidEnemies`; the chief's clip (0x2a4 or 0x2a8), the gang's target and the answer line are not built.
/// @orig 0x00320530 WarriorAttackTactic_Create (unknown)
class WarriorAttackTactic final : public Tactic {
  public:
    /// The tactic for `chief`'s crew, with the level's brains and formations (both must outlive it).
    WarriorAttackTactic(double chief, ScriptServices& services, Formations& formations);
    /// Gives the members their goals.
    void start(Gang& gang) override;

  private:
    double m_chief;
    ScriptServices* m_services;
    Formations* m_formations;
};

/// The hold tactic's radius (`0x00313400` is given 1.5), metres.
inline constexpr float kCrewHoldRadius = 1.5F;

/// `GoalHoldPosition(point, radius)` (type `0x36`, vtable `0x00540510`): keeps to `radius` round `point` and fights
/// from there. With no enemy it walks back (gait 2) when outside the radius. An enemy not targeting it: it turns to him
/// when more than 60° off. One targeting it: from inside the radius (or when it cannot get back) a fight goal (4000
/// ms) once he is in reach and sight, else a turn to him; from outside, a walk back to within 0.5 m. **Coney
/// stand-ins**: the 30 % fidget every 3 s, the fight-stance shuffle and the move round an object in the way are not
/// built; its enemy is the best-scoring one (pickBestEnemy(), the melee terms), "in reach" is within the far melee
/// range × 1.1 and "in sight" a clear straight line; its Suspend stops the move whether or not the gang leader
/// stands idle.
/// @orig 0x002be590 Goal_HoldPosition (unknown)
/// @orig 0x002be640 HoldPositionGoal_Init (unknown)
class HoldPositionGoal final : public Goal {
  public:
    /// Holds `point` within `radius`.
    HoldPositionGoal(anim::Vec3 point, float radius) : Goal(GoalType::HoldPosition), m_point(point), m_radius(radius) {}

    /// The update (above).
    /// @orig 0x002be818 HoldPositionGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Stops the move.
    /// @orig 0x002be688 HoldPositionGoal_End (unknown)
    void end(Brain& brain) override;

    /// The point and the radius.
    [[nodiscard]] anim::Vec3 point() const { return m_point; }
    [[nodiscard]] float radius() const { return m_radius; }

  private:
    anim::Vec3 m_point;
    float m_radius;
};

/// The hold command's tactic (command 3, type 3, vtable `0x005439e0`): every member that is not the chief, not a
/// player and not down is flushed and holds his own position (HoldPositionGoal, radius kCrewHoldRadius), then turns
/// to a heading spread round the chief's (360° / (members − 1) apart), after 0-1 s with enemies about, else 2-4 s.
/// It never ends by itself; events 17 (with no other human), 19 and 22 (with argument 1) give that member his hold
/// again. Its type is below `0x12`, so the members keep their own goals (Tactic::keepsOwnGoals()). **Coney
/// stand-ins**: the answering line (`0x85`) and event 20's look at a fighter and taunt are not built; "enemies about"
/// is read as any member having a fightable enemy.
/// @orig 0x00313400 WarriorHoldTactic_Create (unknown)
/// @orig 0x00313508 HoldTactic_GiveGoals (unknown)
/// @orig 0x00313718 HoldTactic_GiveMemberGoal (unknown)
class WarriorHoldTactic final : public Tactic {
  public:
    /// The tactic for `chief`'s crew, with the level's brains (which must outlive it).
    WarriorHoldTactic(double chief, ScriptServices& services);
    /// Gives the members their goals and turns.
    void start(Gang& gang) override;
    /// Events 17, 19 and 22 (above).
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

  private:
    // Gives `member` the hold at his own position, flushed first.
    static void hold(Brain& member);

    double m_chief;
    ScriptServices* m_services;
};

/// The chief's automatic commands (`WarChief_AutoCommand`), run every update of his brain: what to dispatch, forced,
/// given his last command. Under follow: attack 1.5 s after his target came within his far melee range while he has
/// attackers. Under attack or defend: follow again 1.5 s after no member of his gang has an enemy. **Coney
/// stand-ins**: Coney's player brain keeps no target, so his nearest attacker stands in for it; defend while he mugs,
/// tags or grabs from behind, the player modes 2 and 3, steal at a store flag and the cuffed test are not built.
/// @orig 0x00303988 WarChief_AutoCommand (unknown)
class CrewOrders {
  public:
    /// The command to dispatch for `chief` at `nowMs` given `last`, the last command he gave; nothing to leave it.
    [[nodiscard]] std::optional<int> update(const Brain& chief, int last, std::uint64_t nowMs);

  private:
    std::optional<std::uint64_t> m_attackSince; // when the attack condition began to hold
    std::optional<std::uint64_t> m_calmSince;   // when the gang last had no enemy
};

/// How long the automatic commands wait, ms.
inline constexpr std::uint64_t kCrewAutoDelayMs = 1500;

} // namespace coney::ai
