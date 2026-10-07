// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "ai/goal.h"
#include "animation/anim_math.h"

// The goals of the story's fourth mission (`level34`): the rioters who roam, smash, loot and pick fights before they
// leave (`GoalRiot`), and the bartenders and shopkeepers who stand their ground throwing bottles at enemies
// (`GoalStationaryThrower`). Each is driven by its brain as the other goals are (ai/goal.h); what they ask of the
// level beyond the brain comes in as functions, which the level's scripted brains give them.
// Research: docs/research/ai.md#riot, docs/references/bindings/ai.md#goalriot,
// docs/references/bindings/ai.md#goalstationarythrower

namespace coney::ai {

class Brain;
class ScriptServices;

/// What `GoalRiot` asks of the rioter (its defaults are the binding's).
struct RiotOrder {
    float radius = 15.0F;       ///< `+0x2c`: it may decide while the nearest player is this near, metres.
    int actChance = 30;         ///< `+0x32`: percent, at a decision, to smash or loot.
    int acts = 1;               ///< `+0x33`: acts before it leaves.
    int fightChance = 20;       ///< `+0x34`: percent, at a decision, to pick a fight.
    int playerFightChance = 10; ///< `+0x35`: percent, per fight pick, that a human a player controls may be picked.
    bool shout = true;          ///< `+0x3a`: it shouts while roaming and leaving.
};

/// The riot goal's states (goal `+0x36`).
enum class RiotState : std::uint8_t { Roam = 0, Smash = 1, Loot = 2, Leave = 3 };

/// A rioter may decide every this many brain updates.
inline constexpr std::uint64_t kRiotDecisionUpdates = 60;
/// A fight is picked with a human within this many metres.
inline constexpr float kRiotFightRange = 15.0F;
/// A picked fight's duration, ms (`Brain_Fight`'s 8000).
inline constexpr std::uint64_t kRiotFightMs = 8000;
/// The roam counter's start (`+0x39`): its first step is a multiple of kRiotTowardPlayerEvery.
inline constexpr int kRiotRoamCounterStart = 26;
/// Every this many roam destinations, one toward the player (when he is kRiotTowardPlayerRange or more away).
inline constexpr int kRiotTowardPlayerEvery = 27;
/// Every this many roam destinations, one somewhere in the gang's turf.
inline constexpr int kRiotTurfEvery = 79;
/// The toward-player destination lies this many metres from the player.
inline constexpr float kRiotTowardPlayerRange = 15.0F;
/// The wander: a point this far ahead of the rioter, plus the wander vector of length kRiotWanderRadius.
inline constexpr float kRiotWanderAhead = 10.0F;
inline constexpr float kRiotWanderRadius = 2.5F;
/// With no wander point, a random point this far from the rioter.
inline constexpr float kRiotFallbackRange = 5.0F;
/// The roam's move: arrival radius and gait.
inline constexpr float kRiotArrival = 0.5F;
inline constexpr int kRiotGait = 4;
/// After this many failed moves the rioter leaves; every kRiotRetryEvery-th failure it tries again.
inline constexpr int kRiotFailedMovesMax = 30;
inline constexpr int kRiotRetryEvery = 5;

/// A turf box as the riot's turf destination reads it: its centre and radius.
struct TurfCircle {
    anim::Vec3 centre;
    float radius = 0.0F;
};

/// What the riot goal asks of the level. Any left empty is taken as none (no players, no candidates, no turf, no
/// target, no exit), except `inTurf`, which empty takes every point.
struct RiotServices {
    /// Where the players stand (none, one or two).
    std::function<std::vector<anim::Vec3>()> players;
    /// The humans a rioter may pick a fight with, in their slot order (players included).
    std::function<std::vector<Brain*>()> candidates;
    /// Whether a point lies in the turf of the rioter's gang (true for a gang with no turf): `0x0028ff38`.
    std::function<bool(const Brain&, anim::Vec3)> inTurf;
    /// The turf boxes of the rioter's gang.
    std::function<std::vector<TurfCircle>(const Brain&)> turf;
    /// Finds something to smash for the rioter and gives him the goal that smashes it; false when there is nothing.
    std::function<bool(Brain&)> smash;
    /// Finds something to loot for the rioter and gives him the goal that takes it; false when there is nothing.
    std::function<bool(Brain&)> loot;
    /// Sends the rioter out through the nearest enabled exit flag (activity 8) in his gang's turf: pushes its leaving
    /// goal over the riot's; false when there is none.
    std::function<bool(Brain&)> leave;
};

/// `GoalRiot`'s goal (type 84), as docs/research/ai.md#riot reads it. It starts in state 0 (roam), or about half the
/// time straight in a free smash or loot. Each update it finds the nearest player; while he stands outside the gang's
/// turf, or when he is within the order's radius at every kRiotDecisionUpdates-th brain update about half the time, the
/// rioter decides: one draw picks a fight (gang soldiers only; started, it leaves), else an act (smash or loot), else
/// it leaves. Otherwise it roams: toward the player, somewhere in the turf, or a wander 10 m ahead. Acts count down and
/// then it leaves through the nearest exit flag in its gang's turf. **Coney stand-ins** where Coney lacks the part:
/// the shouts, the taunt and the head glances are not made (no speech or glances yet); a roam destination is not
/// dropped to the ground and "on an area" and "reached in a straight line" are both the planner's straight-line test;
/// the move deadline's human `+0x333` term is 0; a running move is replaced, not retargeted; smash and loot are
/// whatever the services find (the scripted story finds none yet); the actions-blocked check is not made.
/// @orig 0x002d0e98 Goal_Riot (unknown)
/// @orig 0x002d0f68 RiotGoal_Init (unknown)
class RiotGoal final : public Goal {
  public:
    /// Rioting by `order`, through `services` (which must outlive it).
    RiotGoal(const RiotOrder& order, const RiotServices& services);
    /// Init's draws: about half the time it starts with a free smash or loot.
    void start(Brain& brain) override;
    /// The decisions, the roam, the acts and the leaving.
    /// @orig 0x002d1c38 RiotGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// Its order.
    [[nodiscard]] const RiotOrder& order() const { return m_order; }
    /// Its state.
    [[nodiscard]] RiotState state() const { return m_state; }
    /// Acts left.
    [[nodiscard]] int actsLeft() const { return m_actsLeft; }
    /// Whether it has decided once.
    [[nodiscard]] bool decided() const { return m_decided; }

  private:
    // The decision: a fight, an act or the leaving, by one draw; `inTurf` is the turf gate's answer.
    void decide(Brain& brain, bool inTurf);
    // One roam update: the deadlines, then a new destination when no move runs or the deadline rolled over.
    void roam(Brain& brain, anim::Vec3 player, float playerDistance);
    // The roam's destination (`0x002d18b8`): toward the player, in the turf, a wander or a random point near; none
    // when all fail.
    [[nodiscard]] std::optional<anim::Vec3> roamPoint(Brain& brain, anim::Vec3 player, float playerDistance);
    // A point in a random turf box of the rioter's gang that passes the turf gate and is reached in a straight line
    // (`0x002d15d0`).
    [[nodiscard]] std::optional<anim::Vec3> turfPoint(Brain& brain);
    // The wander: 10 m ahead of the rioter plus the wander vector, nudged at random; none when it fails the gates.
    [[nodiscard]] std::optional<anim::Vec3> wanderPoint(Brain& brain);
    // Picks a fight with a human within kRiotFightRange, as RiotGoal_TryPickFight does; true when one started.
    // @orig 0x002d1288 RiotGoal_TryPickFight (unknown)
    bool tryPickFight(Brain& brain);
    // The turf gate (an empty service takes every point).
    [[nodiscard]] bool inTurf(const Brain& brain, anim::Vec3 point) const;

    RiotOrder m_order;
    const RiotServices* m_services;
    RiotState m_state = RiotState::Roam;
    anim::Vec3 m_wander;                       // +0x10
    std::uint64_t m_moveDeadlineMs = 0;        // +0x20
    std::uint64_t m_shoutAtMs = 0;             // +0x24
    int m_actsLeft = 0;                        // +0x33
    bool m_decided = false;                    // +0x37
    int m_failedMoves = 0;                     // +0x38
    int m_roamCounter = kRiotRoamCounterStart; // +0x39
};

/// The object types a stationary thrower may throw (ObjGetIndex's ids).
using ThrowerObjects = std::array<std::uint16_t, 8>;

/// The stationary thrower's states (goal `+0x20`).
enum class ThrowerState : std::uint8_t { Return = 0, Throw = 1, Wait = 2 };

/// The thrower keeps to within this many metres of its spot.
inline constexpr float kThrowerSpotRadius = 0.5F;
/// The throw clip.
inline constexpr int kThrowAnim = 0x225;

/// `GoalStationaryThrower`'s goal (type 140): the human keeps returning to where it stood when given the goal (within
/// kThrowerSpotRadius), picks the nearest enemy within three quarters of its sight range, faces it and plays the throw
/// clip, then waits a random 1000 × delay to 1000 × delay + 1000 ms before the next. **Coney stand-ins**: no object
/// leaves its hand (thrown objects are not built; the eight types are kept), and the one wait in five that plays
/// another idle (`0x002fb0f0`) is not built.
/// @orig 0x002eee50 Goal_StationaryThrower (unknown)
/// @orig 0x002eeee0 StationaryThrowerGoal_Init (unknown)
class StationaryThrowerGoal final : public Goal {
  public:
    /// Waiting `delaySeconds` (one byte) after each throw, the clip played through `services` (which must outlive it).
    StationaryThrowerGoal(int delaySeconds, const ThrowerObjects& objects, ScriptServices& services);
    /// Its spot: where the human stands now.
    void start(Brain& brain) override;
    /// The return, the throw and the wait.
    /// @orig 0x002ef2c0 StationaryThrowerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The object types it may throw.
    [[nodiscard]] const ThrowerObjects& objects() const { return m_objects; }
    /// Its state.
    [[nodiscard]] ThrowerState state() const { return m_state; }
    /// Throws made.
    [[nodiscard]] int throws() const { return m_throws; }

  private:
    int m_delaySeconds;
    ThrowerObjects m_objects;
    ScriptServices* m_services;
    ThrowerState m_state = ThrowerState::Return;
    anim::Vec3 m_spot;
    std::uint64_t m_waitUntilMs = 0;
    int m_throws = 0;
};

} // namespace coney::ai
