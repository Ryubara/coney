// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ai/goal.h"
#include "animation/anim_math.h"

// The goals the story's fifth and sixth missions add: guarding a flag, leading a chase along a path, the paced runner
// of the rooftop and street chases (`GoalDevilRun`) and the balcony boss who throws from a ledge. Each is driven by its
// brain as the other scripted goals are (ai/goal.h); the flags and paths come in through FlagServices.
// Research: docs/references/bindings/ai.md, docs/research/ai.md#scripted

namespace coney::ai {

class Brain;
class FlagServices;
class Gangs;
class ScriptServices;

/// `GoalGuardFlag`'s goal (type 43): the guard walks (gait 2) back to the flag whenever it is more than `radius` from
/// it and, on station, turns to `heading` every kGuardFaceUpdates updates. It ends, completed, when the flag no
/// longer exists; its callback is scheduled with (human, completed). **Coney stand-in**: the event that pushes the
/// LeftTurf goal over it is not routed, and the time word is kept, not read.
/// @orig 0x002b7a90 GuardFlagGoal_Init (unknown)
class GuardFlagGoal final : public Goal {
  public:
    /// The heading is re-applied every this many updates.
    static constexpr std::uint64_t kGuardFaceUpdates = 30;

    /// Guarding `flag` (found through `services`, which must outlive it) within `radius`, facing `headingDegrees`
    /// (-1 for none); `callback` may be empty.
    GuardFlagGoal(double flag, float radius, int headingDegrees, std::string callback, int timeMs,
                  FlagServices& services, ScriptServices* scripts);
    /// The walk back and the facing.
    /// @orig 0x002b7c18 GuardFlagGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Schedules the callback.
    void end(Brain& brain) override;

    /// The time word (`+0x08`), -1 for none.
    [[nodiscard]] int timeMs() const { return m_timeMs; }

  private:
    double m_flag;
    float m_radius;
    int m_heading;
    std::string m_callback;
    int m_timeMs;
    FlagServices* m_services;
    ScriptServices* m_scripts;
    bool m_completed = false;
};

/// The runner's choice of a chaser: every this many updates the nearest living enemy within kLeadChaseRange.
inline constexpr std::uint64_t kLeadChaseChooseUpdates = 10;
inline constexpr float kLeadChaseRange = 50.0F;

/// `GoalLeadChase`'s goal (type 73): the runner leads its chaser along a path of flags. While the chaser is within
/// `waitDistance` it runs on to the next point, sprinting while its stamina is above 60% and dropping back to a run at
/// 20% or less; farther away it stops and turns to face the chaser when more than 15° off. It ends at the path's last
/// point. Its threat response is set to 0, so it does not fight back.
/// @orig 0x002e0b48 LeadChaseGoal_Init (unknown)
class LeadChaseGoal final : public Goal {
  public:
    /// Along `points` (flag handles, found through `services`), waiting beyond `waitDistance` metres; the chaser
    /// is found among `gangs`' members (both must outlive it).
    LeadChaseGoal(std::vector<double> points, float waitDistance, FlagServices& services, const Gangs& gangs);
    /// Sets the threat response to 0.
    void start(Brain& brain) override;
    /// The choice of chaser, the run or the wait.
    /// @orig 0x002e0dc8 LeadChaseGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The index of the point it runs to.
    [[nodiscard]] std::size_t current() const { return m_current; }
    /// Whether it sprints.
    [[nodiscard]] bool sprinting() const { return m_sprinting; }

  private:
    std::vector<double> m_points;
    float m_waitDistance;
    FlagServices* m_services;
    const Gangs* m_gangs = nullptr;
    Brain* m_chaser = nullptr;
    std::size_t m_current = 0;
    bool m_sprinting = true;
};

/// `GoalDevilRun(human, path, gang, gait, attackDistance, paceDistance, maxSpeed, urgency, hostile)`'s numbers.
struct DevilRunOrder {
    int gang = 0;               ///< The gang it paces itself against.
    int gait = 4;               ///< Its slowest speed is this gait's.
    float attackDistance = 0;   ///< Hostile only: metres along the path at which it attacks a running chaser.
    float paceDistance = 10.0F; ///< Metres over which the speed blends.
    float maxSpeed = 10.0F;     ///< m/s at a blend of 1.
    float urgency = 0.0F;       ///< Kept, not read (its meaning is not traced).
    bool hostile = false;       ///< A pursuer that speeds up behind and attacks; else a runner that flees.
};

/// The devil run's checks, in its own update count: the hindmost segment, the chaser's choice and the pace.
inline constexpr std::uint32_t kDevilHindmostUpdates = 17;
inline constexpr std::uint32_t kDevilChooseUpdates = 29;
inline constexpr std::uint32_t kDevilPaceUpdates = 7;
/// The count a resumed devil run starts from.
inline constexpr std::uint32_t kDevilResumeCount = 16;
/// The move to the path's last point arrives within this many metres.
inline constexpr float kDevilArrival = 1.5F;

/// `GoalDevilRun`'s goal (type 152, docs/research/ai.md#devil-run): the runner moves for the path's last point (a move
/// action that plans its own route) while pacing itself against a gang. The path measures progress: every
/// kDevilHindmostUpdates the gang's hindmost segment, every kDevilChooseUpdates the chaser (the live member farthest
/// back along it), every kDevilPaceUpdates the speed, between its gait's and `maxSpeed` by the distance to the
/// chaser over `paceDistance`: a friendly runner the faster the closer the chaser, a hostile pursuer the faster the
/// farther behind it falls; a hostile one that catches a chaser attacks it. It ends when the gang has no (live)
/// member. While it runs its threat response is 0 and its field of view all round. **Coney stand-ins**: the attack
/// fights the chaser (the melee and engage-enemy goals it pushes are not built), and the urgency's brain byte is not
/// kept.
/// @orig 0x002e1850 DevilRunGoal_Init (unknown)
class DevilRunGoal final : public Goal {
  public:
    /// Along `points` (flag handles, found through `services`) with `order`, its gang found in `gangs` (both must
    /// outlive it).
    DevilRunGoal(std::vector<double> points, const DevilRunOrder& order, FlagServices& services, const Gangs& gangs);
    /// Saves the threat response and the field of view, sees all round, the speed the gait's; then resume().
    /// @orig 0x002e18d8 DevilRunGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Threat response 0, the count kDevilResumeCount.
    /// @orig 0x002e1998 DevilRunGoal_Resume (unknown)
    void resume(Brain& brain) override;
    /// Restores the threat response and the field of view.
    /// @orig 0x002e1950 DevilRunGoal_End (unknown)
    void end(Brain& brain) override;
    /// The hindmost segment, the chaser, the pace and the move.
    /// @orig 0x002e2230 DevilRunGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The speed it runs at, m/s.
    [[nodiscard]] float speed() const { return m_speed; }
    /// The gang's hindmost segment (its first point's index), once measured.
    [[nodiscard]] std::optional<std::size_t> hindmost() const { return m_hindmost; }
    /// The chaser, or null.
    [[nodiscard]] const Brain* chaser() const { return m_chaser; }

  private:
    // The path's points found now (those the services cannot find left out).
    [[nodiscard]] std::vector<anim::Vec3> points() const;
    // The pace (0x002e1e10): the speed from the distance to the chaser, or a hostile runner's attack; true when it
    // attacked.
    bool pace(Brain& brain, const std::vector<anim::Vec3>& path);

    std::vector<double> m_points;
    DevilRunOrder m_order;
    FlagServices* m_services;
    const Gangs* m_gangs = nullptr;
    Brain* m_chaser = nullptr;
    std::optional<std::size_t> m_hindmost;
    std::uint32_t m_count = 0;
    float m_speed = 0.0F;
    int m_savedThreat = 0;
    float m_savedFieldOfView = 0.0F;
};

/// `GoalBigLedgeThrower(human, targets, objects, cycles, delayMs, taunt, anim)`'s values.
struct LedgeThrowerOrder {
    std::array<double, 3> targets{}; ///< Flags thrown at (0 unused).
    std::array<int, 8> objects{};    ///< Object type ids (-1 unused).
    int cycles = 1;                  ///< Throws a round.
    std::uint32_t delayMs = 1000;    ///< The wait after each throw.
    bool taunt = false;              ///< Starts with a taunt.
};

/// `GoalBigLedgeThrower`'s goal (type 138), the balcony boss: it keeps to the spot where it stood (walking back when
/// more than kLedgeReturnDistance away), and throws at the nearest enemy (or a target flag) `cycles` times a round,
/// waiting `delayMs` after each, then turns on the nearest enemy and starts a new round. **Coney stand-in**: Coney has
/// no thrown objects yet, so a throw is the turn to the target and the wait, with nothing picked up or thrown.
/// @orig 0x002edff8 BigLedgeThrowerGoal_Init (unknown)
class BigLedgeThrowerGoal final : public Goal {
  public:
    /// Walking back beyond this many metres from its spot.
    static constexpr float kLedgeReturnDistance = 0.5F;
    /// The states (`+0x40`).
    enum class State : std::uint8_t { Taunt, Return, PickUp, Throw, Pause };

    /// With `order`, finding target flags through `services` and enemies among `gangs`' members.
    BigLedgeThrowerGoal(const LedgeThrowerOrder& order, FlagServices& services, const Gangs& gangs);
    /// Keeps the spot where it stands now.
    void start(Brain& brain) override;
    /// The states (above).
    /// @orig 0x002ee7b0 BigLedgeThrowerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The state it is in.
    [[nodiscard]] State state() const { return m_state; }
    /// Throws in this round.
    [[nodiscard]] int throws() const { return m_throws; }

  private:
    LedgeThrowerOrder m_order;
    FlagServices* m_services;
    const Gangs* m_gangs = nullptr;
    anim::Vec3 m_spot;
    State m_state = State::PickUp;
    int m_throws = 0;
    std::uint64_t m_waitUntilMs = 0;
};

} // namespace coney::ai
