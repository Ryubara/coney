// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// A brain's goal: a long-lived intention (fight this human, block for a while, lie down until the knockdown is over)
// that a brain keeps on its stack of ten, or as its reaction goal. The brain drives it (ai::Brain): it starts the goal,
// or resumes it after the goal above it was popped, only when its action queue is empty, then processes it.
// Research: docs/research/ai.md#goals, docs/research/ai.md#update-goals

namespace coney::ai {

class Brain;

/// A fight's duration meaning none (`GoalFight` passes −1, docs/research/ai.md#fight-durations).
inline constexpr int kNoFightLimit = -1;
/// The duration of every fight goal a Melee goal pushes, ms (its `+0x18`).
inline constexpr int kMeleeFightMs = 4000;

/// The goal types Coney builds, by the original's type ids (the vtable's `+0x0c`, docs/research/ai.md#goals).
enum class GoalType : std::uint8_t {
    Idle = 0x00,               ///< IdleGoal: stands in place.
    MoveToFlag = 0x01,         ///< MoveToFlagGoal.
    MoveToExitFlag = 0x02,     ///< MoveToExitFlagGoal: leaves the scene through an exit flag.
    MoveToUseFlag = 0x04,      ///< MoveToUseFlagGoal.
    MoveToHuman = 0x06,        ///< MoveToHumanGoal.
    Melee = 0x08,              ///< MeleeGoal (a fight's), TacticMeleeGoal (the attack tactic's stand-in).
    EngageEnemy = 0x0b,        ///< EngageEnemyGoal.
    Fight = 0x0f,              ///< FightGoal.
    Spectate = 0x10,           ///< SpectateGoal: stands and watches for a while.
    ReactGrabbing = 0x12,      ///< Reaction: grabbing (state `0xc0`).
    ReactTackling = 0x13,      ///< Reaction: tackling (`0x400`).
    ReactGrabbed = 0x14,       ///< Reaction: grabbed (`0x30`) or mugged (`0x200`).
    ReactTackled = 0x15,       ///< Reaction: tackled (`0x800`).
    ReactKnockedDown = 0x17,   ///< Reaction: knocked down (`0x80000`).
    ReactStunned = 0x18,       ///< Reaction: stunned (`0x100000`) and not down.
    Block = 0x1b,              ///< BlockGoal.
    HelpRespond = 0x1d,        ///< HelpRespondGoal: answers a help call.
    GrabTarget = 0x1f,         ///< GrabTargetGoal: walks up to a human and holds it.
    PlayAnimation = 0x21,      ///< PlayAnimationGoal: a scene.
    PlayDynAnimation = 0x22,   ///< PlayDynAnimationGoal.
    PlayDynIdle = 0x23,        ///< PlayDynIdleGoal.
    PlayGenAnim = 0x26,        ///< PlayGenAnimGoal: one generic clip.
    GuardFlag = 0x2b,          ///< GuardFlagGoal: keeps to a flag.
    TrackHuman = 0x30,         ///< TrackHumanGoal.
    FollowPlayer = 0x32,       ///< FollowPlayerGoal: keeps to the war chief's formation.
    FollowFormation = 0x33,    ///< FollowFormationGoal: walks to its formation slot.
    FollowAndAttack = 0x34,    ///< FollowAndAttackGoal: fights the best enemy near the war chief, or stays with him.
    HoldPosition = 0x36,       ///< HoldPositionGoal: keeps to a radius round a point and fights from there.
    TravelPath = 0x38,         ///< TravelPathGoal.
    FindEnemy = 0x41,          ///< FindEnemyGoal: looks for an enemy to fight.
    AreaWalker = 0x47,         ///< AreaWalkerGoal: strolls round a centre.
    LeadChase = 0x49,          ///< LeadChaseGoal: leads a chaser along a path.
    BumLogic = 0x4f,           ///< BumLogicGoal.
    Peddler = 0x50,            ///< PeddlerGoal: a vendor beckoning passers-by.
    Riot = 0x54,               ///< RiotGoal: roams, smashes, loots and picks fights, then leaves.
    AddressPerson = 0x57,      ///< AddressPersonGoal.
    ThrowObject = 0x5d,        ///< ThrowObjectGoal.
    Pedestrian = 0x69,         ///< PedestrianGoal: wanders the flag network (`FlagNetTraverse`).
    PedestrianReaction = 0x6b, ///< PedestrianReactionGoal: a pedestrian reacting to trouble (mode 9: flight).
    Dealer = 0x80,             ///< DealerGoal.
    Shopkeeper = 0x82,         ///< ShopkeeperGoal.
    BigBrawler = 0x84,        ///< BigBrawlerGoal: a boss who fights, tires after six hits and (Vargas) fetches objects.
    BigThrower = 0x87,        ///< BigThrowerGoal: a boss who throws objects from a flag.
    BigLedgeThrower = 0x8a,   ///< BigLedgeThrowerGoal: the balcony boss.
    StationaryThrower = 0x8c, ///< StationaryThrowerGoal: throws at enemies from its spot.
    Tired = 0x90,             ///< TiredGoal: a boss stunned and open to hits for a while.
    DevilRun = 0x98,          ///< DevilRunGoal: a chase's paced runner.
    Backoff = 0x9b,           ///< BackoffGoal.
    Boxer = 0x9e,             ///< BoxerGoal: boxes a target for ever.
};

/// What a goal's process() returns (`Goal_Process`): stop for this update, process the stack's top again in the same
/// update, or done (the brain pops it and processes the new top in the same update).
enum class GoalStatus : std::uint8_t {
    Stop = 0,
    Again = 1,
    Done = 2,
};

/// A goal. Subclasses override what they do; every hook but process() does nothing by default.
class Goal {
  public:
    /// A goal of `type`, not started.
    explicit Goal(GoalType type) : m_type(type) {}
    Goal(const Goal&) = delete;
    Goal& operator=(const Goal&) = delete;
    Goal(Goal&&) = delete;
    Goal& operator=(Goal&&) = delete;
    virtual ~Goal() = default;

    /// Its type id.
    [[nodiscard]] GoalType type() const { return m_type; }
    /// Whether it has been started (`+0x04`).
    [[nodiscard]] bool started() const { return m_started; }

    /// Starts it (vtable `+0x24`), once, with the action queue empty.
    virtual void start(Brain& /*brain*/) {}
    /// Takes it up again after the goal above it was popped (`+0x34`), with the action queue empty.
    virtual void resume(Brain& /*brain*/) {}
    /// A goal is pushed above it (`Goal_Suspend`).
    virtual void suspend(Brain& /*brain*/) {}
    /// It is popped or flushed (`+0x2c`).
    virtual void end(Brain& /*brain*/) {}
    /// One update's work (`+0x44`).
    [[nodiscard]] virtual GoalStatus process(Brain& brain) = 0;
    /// Its adjustment of `scorer`'s score for an enemy it may pick (vtable `+0x54`, ai::pickBestEnemy()): by default
    /// the previous target (`previous`) gets TargetingPoints::previousTarget more.
    /// @orig 0x0029f3a8 Goal_AdjustEnemyScoreDefault (unknown)
    [[nodiscard]] virtual float adjustEnemyScore(const Brain& scorer, const Brain& candidate, const Brain* previous,
                                                 float score) const;

  private:
    friend class Brain;

    GoalType m_type;
    bool m_started = false;       // +0x04
    bool m_resumePending = false; // the goal above it was popped: resume() before the next process()
};

} // namespace coney::ai
