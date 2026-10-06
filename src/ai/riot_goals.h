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
// Research: docs/references/bindings/ai.md#goalriot, docs/references/bindings/ai.md#goalstationarythrower

namespace coney::ai {

class Brain;
class ScriptServices;

/// What `GoalRiot` asks of the rioter (its defaults are the binding's).
struct RiotOrder {
    float radius = 15.0F;     ///< `+0x2c`: it acts out while the nearest player is this near, metres.
    int actChance = 30;       ///< `+0x32`: percent, at a decision, to smash or loot.
    int acts = 1;             ///< `+0x33`: acts before it leaves.
    int fightChance = 20;     ///< `+0x34`: percent, at a decision, to pick a fight.
    int gangFightChance = 10; ///< `+0x35`: percent that a gang rioter's fight may be with a gang member too.
    bool shout = true;        ///< `+0x3a`: it shouts while roaming and leaving.
};

/// The riot goal's states (goal `+0x36`).
enum class RiotState : std::uint8_t { Roam = 0, Smash = 1, Loot = 2, Leave = 3 };

/// A rioter decides every this many updates.
inline constexpr std::uint64_t kRiotDecisionUpdates = 60;
/// A fight is picked with a human within this many metres.
inline constexpr float kRiotFightRange = 15.0F;
/// **Coney stand-in**: how far a roaming rioter strays from where the goal started, metres (the roam is not traced).
inline constexpr float kRiotRoamRange = 8.0F;

/// What the riot goal asks of the level.
struct RiotServices {
    /// Where the players stand (none, one or two).
    std::function<std::vector<anim::Vec3>()> players;
    /// The brains a rioter may pick a fight with.
    std::function<std::vector<Brain*>()> candidates;
    /// Sends the human out through the nearest exit flag (type 8): pushes its leaving goal over the riot's.
    std::function<void(Brain&)> leave;
};

/// `GoalRiot`'s goal (type 84): the human roams, and at a decision (every kRiotDecisionUpdates updates, about half the
/// time) while a player is within the order's radius it may pick a fight with a human within kRiotFightRange (only a
/// gang member does, with a civilian, or with the gang-fight chance a gang member too), or smash or loot. After its
/// acts, or once a fight starts, it leaves through the nearest exit flag. **Coney stand-ins** where the page is
/// silent or Coney lacks the part: the roam walks to points within kRiotRoamRange of its start; the smash and loot
/// searches (breakables and items within 20 m) find nothing, as Coney does not hook them yet, so no act is made; the
/// fight is not limited to 8 s; the shouts (speech `0x59`, `0x11`) are not said; the decision's gang test
/// (`0x0028ff58`) is taken as passed and "about half the time" as 50 %.
/// @orig 0x002d0e98 Goal_Riot (unknown)
/// @orig 0x002d0f68 RiotGoal_Init (unknown)
class RiotGoal final : public Goal {
  public:
    /// Rioting by `order`, through `services` (which must outlive it).
    RiotGoal(const RiotOrder& order, const RiotServices& services);
    /// Notes where it started, the centre of its roam.
    void start(Brain& brain) override;
    /// The roam, the decisions and the leaving.
    /// @orig 0x002d1c38 RiotGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// Its order.
    [[nodiscard]] const RiotOrder& order() const { return m_order; }
    /// Its state.
    [[nodiscard]] RiotState state() const { return m_state; }

  private:
    // Picks a fight with a human within kRiotFightRange, as RiotGoal_TryPickFight does; true when one started.
    // @orig 0x002d1288 RiotGoal_TryPickFight (unknown)
    bool tryPickFight(Brain& brain);
    // Whether a player stands within the order's radius.
    [[nodiscard]] bool playerNear(const Brain& brain) const;

    RiotOrder m_order;
    const RiotServices* m_services;
    RiotState m_state = RiotState::Roam;
    anim::Vec3 m_centre;
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
