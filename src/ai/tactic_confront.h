// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "ai/goal.h"
#include "ai/tactic.h"

// TacticConfront: a gang squares up to another. Its members close in on the other gang's leader, and the tactic tells
// its callback how near the two leaders are: in approach range (7), in critical range (1, where the arena scripts
// switch to an attack), out of approach range again (2), or apart with no way between (9).
// Research: docs/research/ai.md#tactic-kinds, docs/references/bindings/ai.md#tacticconfront

namespace coney::ai {

/// The confront tactic's type id, and its members' goal's (`Confront`, 60).
inline constexpr int kConfrontTactic = 0x23;
inline constexpr GoalType kConfrontGoal = static_cast<GoalType>(60);
/// Its codes: in critical range, out of approach range again, in approach range, apart (`TacticGetString`).
inline constexpr int kTacInCriticalRange = 1;
inline constexpr int kTacLeftRange = 2;
inline constexpr int kTacInRange = 7;
inline constexpr int kTacApart = 9;
/// The damage and attacked codes the events fire.
inline constexpr int kTacDamage = 5;
inline constexpr int kTacAttacked = 6;
/// How often the process measures.
inline constexpr std::uint64_t kConfrontPeriodMs = 250;
/// The event a member hears when attacked (`0x10`, a warning of an attack).
inline constexpr int kConfrontAttackedEvent = 0x10;

/// `TacticConfront`'s arguments as the tactic keeps them.
struct ConfrontSettings {
    int targetGang = -1;             ///< −1: the first member's target's gang.
    float approachRange = 10.0F;     ///< Metres.
    float criticalRange = 2.0F;      ///< Metres.
    std::uint32_t confrontation = 0; ///< Non-zero: the members' goals keep the approach range.
};

/// The confront goal (type 60): **Coney's stand-in**, its code not being traced. The member closes on the human named
/// by its target (MoveToHumanAction, 1 s at a time) until within its distance, then waits there.
class ConfrontGoal final : public Goal {
  public:
    /// Closing to `distance` metres of the brain's target.
    explicit ConfrontGoal(float distance) : Goal(kConfrontGoal), m_distance(distance) {}

    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The distance it keeps.
    [[nodiscard]] float distance() const { return m_distance; }

  private:
    float m_distance;
};

/// The leader of `gang` (gang `+0x44`, `0x00165678`): **Coney choice**, the first member alive, not down and not a
/// player's, else the first alive and not down; null when none.
[[nodiscard]] Brain* gangLeader(const Gang& gang);

/// The confront tactic (type `0x23`, vtable `0x00543740`).
/// @orig 0x0030e670 TacticConfront_Init (unknown)
class TacticConfront final : public Tactic {
  public:
    /// A confrontation by `settings` calling `callback` (empty for none).
    TacticConfront(const ConfrontSettings& settings, std::string callback)
        : Tactic(kConfrontTactic, std::move(callback)), m_settings(settings) {}

    /// Finds the target gang (the first member's target's when −1; with none the tactic does nothing), and gives each
    /// AI member the confront goal on the other leader at the critical range (the approach range with a non-zero
    /// confrontation). **Coney choices**: the posture anims, the spot line and the formation are not built.
    /// @orig 0x0030ec20 TacticConfront_Start (unknown)
    void start(Gang& gang) override;
    /// Every kConfrontPeriodMs: the leaders' distance against the ranges: within the critical range 1, within the
    /// approach range 7, out of it after being in 2. **Coney choices**: the gangs' radii are 0 (the bounds are not
    /// built) and there is always a way between the leaders (the route test is not built), so 9 never fires; the
    /// facing postures every 500 ms are not built.
    /// @orig 0x0030eec0 TacticConfront_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// Damage (1) fires 5, an attack warning (`0x10`) 6.
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

    /// The gang it confronts; −1 before the start or when none was found.
    [[nodiscard]] int targetGang() const { return m_target; }

  private:
    ConfrontSettings m_settings;
    int m_target = -1;
    bool m_inRange = false;
    std::uint64_t m_nextMs = 0;
};

} // namespace coney::ai
