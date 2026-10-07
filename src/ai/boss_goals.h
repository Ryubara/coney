// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "ai/goal.h"

// The Diego and Vargas fight's boss goals (level 5, mission 7): the BigBrawler (a boss who taunts, runs in, fights,
// tires after six hits and, for Vargas in stage 3, fetches objects from a flag), the BigThrower (Vargas in stage 2,
// throwing objects from a flag) and the tired goal (the boss stunned and open to hits for a while, and the break that
// ends a stage). The tactic that gives them is ai/tactic_boss.h.
// Research: docs/research/ai.md#boss-diego-vargas, docs/research/ai.md#boss-throw, docs/research/ai-goals.md#goal-tired

namespace coney::ai {

class Brain;
class FlagServices;
class ScriptServices;

/// The two bosses' character classes (`HuCreate`'s types): Diego the brawler in every stage, Vargas the thrower in
/// stage 2 and a brawler in stage 3 (docs/research/ai.md#boss-diego-vargas).
inline constexpr int kDiegoClass = 120;
inline constexpr int kVargasClass = 119;

/// The boss anims: the rage taunt, the shove, Vargas's pick-up, the break's three clips and the overhead throw.
namespace boss_anim {
inline constexpr int kRage = 643;      ///< `ANIM_RAGE_START`.
inline constexpr int kShove = 653;     ///< `ANIM_SPECIAL_ATTACK1_FRONT`.
inline constexpr int kPickUp = 549;    ///< `ANIM_GHETTO_PICK_UP`.
inline constexpr int kBreak = 671;     ///< `ANIM_SPECIAL_IDLE`: the break's start.
inline constexpr int kBreakLoop = 672; ///< `ANIM_SPECIAL_IDLE_START`: the break's hold.
inline constexpr int kBreakEnd = 673;  ///< `ANIM_SPECIAL_IDLE_END`.
inline constexpr int kThrow = 505;     ///< The overhead throw, standing (set 4).
} // namespace boss_anim

/// The boss lines (speech commands).
namespace boss_line {
inline constexpr int kTaunt = 0x57;        ///< BigBrawler's stage-1 opening.
inline constexpr int kThrowerTaunt = 0x11; ///< BigThrower's opening.
inline constexpr int kHurt = 8;            ///< A hit while running; the tired man while stunned.
inline constexpr int kShoveA = 11;         ///< The shove's two lines.
inline constexpr int kShoveB = 14;
inline constexpr int kTired = 0x95;   ///< The tired goal's first line.
inline constexpr int kRecover = 0x96; ///< The tired goal's recovery.
inline constexpr int kBreak = 0x22;   ///< The break.
} // namespace boss_line

/// A boss's tables per stage (`TacticBossScenarioA`'s per-boss tables, docs/references/bindings/ai.md): seconds tired,
/// the percent of his maximum health the players may take while tired, the never-read prone bytes and the cycles (the
/// BigThrower's throws before he tires; stored and never read by the BigBrawler). The defaults are those of a goal made
/// without tables.
struct BossTables {
    std::array<int, 3> fatigue{5, 4, 3};
    std::array<int, 3> damage{30, 20, 10};
    std::array<int, 3> prone{100, 100, 100};
    int cycles = 0;
};

/// The tired goal (type `0x90`): the boss stunned until his fatigue time passes or the players have taken the damage
/// limit of his health, then back on his feet (line `0x96`, the rage clip). While it runs, the tactic may start the
/// break that ends a stage (startBreak()).
/// @orig 0x002e7970 TiredGoal_Init (unknown)
class TiredGoal final : public Goal {
  public:
    /// Tired for `fatigueMs`, or until he has lost `damagePercent` % of his maximum health; lines and clips through
    /// `services` (null: none).
    TiredGoal(std::uint64_t fatigueMs, int damagePercent, ScriptServices* services)
        : Goal(GoalType::Tired), m_fatigueMs(fatigueMs), m_damagePercent(damagePercent), m_services(services) {}

    /// Saves his god mode, no-reaction and unstunnable switches and clears them, notes his health and stuns him until
    /// the deadline.
    /// @orig 0x002e7a68 TiredGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Line `0x95` once, then line 8 while he stands stunned; at the deadline or the damage limit his saved god mode
    /// back, hit reactions off and the stun ended; once he is free, god mode on, line `0x96` and the rage clip, done
    /// on the next update. While a break runs it plays the break's clips and waits.
    /// @orig 0x002e7f50 TiredGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Restores the saved switches and ends the stun.
    /// @orig 0x002e7b30 TiredGoal_End (unknown)
    void end(Brain& brain) override;

    /// The break (`TiredGoal_StartBreak`): the stun ended, hit reactions off and god mode on, the break clip 671 then
    /// its hold 672, line `0x22`, and his health set to exactly `healthPercent`.
    /// @orig 0x002e7c18 TiredGoal_StartBreak (unknown)
    void startBreak(Brain& brain, float healthPercent);
    /// Whether the break runs (`+0x21`).
    [[nodiscard]] bool breaking() const { return m_breaking; }
    /// Whether the break's start clip still plays (held flag `0x20000`): the tactic answers 18 meanwhile.
    [[nodiscard]] bool breakClipPlaying() const { return m_breaking && !m_breakHoldAtMs.has_value(); }
    /// Whether the break is done (`TiredGoal_IsBreakDone`): its hold has played for 2 s.
    /// @orig 0x002e7e68 TiredGoal_IsBreakDone (unknown)
    [[nodiscard]] bool breakDone(const Brain& brain) const;
    /// Ends a break (`TiredGoal_EndBreak`): the end clip 673, god mode and no-reaction off.
    /// @orig 0x002e7da8 TiredGoal_EndBreak (unknown)
    void endBreak(Brain& brain);

  private:
    // Where the goal is: stunned, recovering (waiting to be free), recovered (done next update).
    enum class Phase : std::uint8_t { Stunned, Recovering, Recovered };

    std::uint64_t m_fatigueMs;
    int m_damagePercent;
    ScriptServices* m_services;
    Phase m_phase = Phase::Stunned;
    std::uint64_t m_deadlineMs = 0; // the stun's end
    int m_startHealth = 0;          // +0x18
    int m_damageLimit = 0;          // +0x1a
    bool m_saidTired = false;
    bool m_savedGod = false;
    bool m_savedNoReact = false;
    bool m_savedUnstunnable = false;
    bool m_breaking = false;                      // +0x21
    std::optional<std::uint64_t> m_breakHoldAtMs; // when the hold 672 began
};

/// What a BigBrawler is given: his tables, the stage, the flag of Vargas's object cycle (0 for none) and the eight
/// object types he may fetch.
struct BigBrawlerOrder {
    BossTables tables;
    int stage = 1;
    double flag = 0;
    std::array<std::uint16_t, 8> objects{};
};

/// The BigBrawler goal (type `0x84`): stage 1 opens with a taunt; then he picks the best enemy, runs in (EngageEnemy
/// with a 2.5 m run-in) or strikes with the cross + square special close by, and fights: a kind picked and kept until
/// it can be pressed in reach. Six hits tire him (TiredGoal); with a flag he then fetches an object from it.
/// @orig 0x002e8288 BigBrawlerGoal_Init (unknown)
class BigBrawlerGoal final : public Goal {
  public:
    /// The states (`+0x18`): the taunt, the engage, the fight and the flag cycle.
    enum class State : std::uint8_t { Taunt = 0, Engage = 1, Fight = 2, FlagCycle = 3 };

    /// A BigBrawler of `order`; lines, clips and flags through `services` and `flags` (null: none).
    BigBrawlerGoal(const BigBrawlerOrder& order, ScriptServices* services, const FlagServices* flags);

    /// Threat response 0, no pick-ups, all-round sight, not reachable, the switches `0x223c0` (ungrabbable,
    /// ungroundable, unstunnable, reduced reactions, keep weapon, auto escape), and the turn boost saved.
    /// @orig 0x002e8418 BigBrawlerGoal_Start (unknown)
    void start(Brain& brain) override;
    /// One update by state (above). **Coney stand-ins**: the enemies are his gang's enemies within his sight range
    /// (the enemy scan is ai-core's); the busy man's line, event 24 and the help call are not built; no object
    /// appears in his hand (no thrown objects in Coney), so a fetched object is kept as a flag and its throw plays the
    /// throw clip; the turn "toward the camera" turns to player 1.
    /// @orig 0x002e8f78 BigBrawlerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Undoes Start: threat response 2, the field of view 1.92 rad, the turn boost back.
    /// @orig 0x002e8530 BigBrawlerGoal_End (unknown)
    void end(Brain& brain) override;
    /// The goal's score (`BigBrawlerGoal_AdjustEnemyScore`): beyond his sight range ruled out; in the fight beyond
    /// 1.15 × far ruled out (but Diego in stage 3), else 4 per metre inside it, 15 for a player and 10 for a man down;
    /// in the engage 4 per metre of distance and 20 for a player not down.
    /// @orig 0x002e8620 BigBrawlerGoal_AdjustEnemyScore (unknown)
    [[nodiscard]] float adjustEnemyScore(const Brain& scorer, const Brain& candidate, const Brain* previous,
                                         float score) const override;

    /// A hit taken (`BigBrawlerGoal_OnHit`, the tactic's event 1): a running boss shouts; in the fight the hit counts,
    /// and the sixth tires him (with a flag, the flag cycle follows).
    /// @orig 0x002e8bc8 BigBrawlerGoal_OnHit (unknown)
    void onHit(Brain& brain);
    /// An attack announced on him by `attacker` (`BigBrawlerGoal_OnAttackWarning`, event `0x10`): in the fight and not
    /// tired, a grab or tackle he can escape is countered (command 3), a player attacker becomes his target, and an
    /// attacker within 1.5 m of a free boss with 10 % of his health or more is shoved off (line 11 or 14, clip 653).
    /// @orig 0x002e8858 BigBrawlerGoal_OnAttackWarning (unknown)
    void onAttackWarning(Brain& brain, Brain* attacker);

    /// The stage (`BigBrawlerGoal_SetStage`, `+0x3f`).
    /// @orig 0x002e8840 BigBrawlerGoal_SetStage (unknown)
    void setStage(int stage) { m_order.stage = stage; }
    [[nodiscard]] int stage() const { return m_order.stage; }
    [[nodiscard]] State state() const { return m_state; }
    /// Hits counted toward tiring (`+0x45`).
    [[nodiscard]] int hits() const { return m_hits; }
    /// Whether a fetched object is in his hand.
    [[nodiscard]] bool holding() const { return m_holding; }
    /// His fatigue in ms for the stage (`BigBrawlerGoal_GetFatigueMs`) and the damage percent
    /// (`BigBrawlerGoal_GetDamagePercent`).
    /// @orig 0x002e8818 BigBrawlerGoal_GetFatigueMs (unknown)
    /// @orig 0x002e8830 BigBrawlerGoal_GetDamagePercent (unknown)
    [[nodiscard]] std::uint64_t fatigueMs() const;
    [[nodiscard]] int damagePercent() const;

  private:
    // The engage state's decision: the best enemy, run at him or strike close by.
    GoalStatus engage(Brain& brain);
    // The fight state: a throw with an object in hand, else a kind kept until it can be pressed.
    GoalStatus fight(Brain& brain);
    // The flag cycle: to the flag, the pick-up, two metres back.
    GoalStatus flagCycle(Brain& brain);

    BigBrawlerOrder m_order;
    ScriptServices* m_services;
    const FlagServices* m_flags;
    State m_state = State::Engage;
    std::uint64_t m_repickAtMs = 0; // +0x1c
    int m_kind = 45;                // +0x20 (kNoAttackKind)
    int m_hits = 0;                 // +0x45
    int m_flagStep = 0;             // +0x43
    bool m_holding = false;
    int m_savedTurnBoost = 0; // +0x40
    std::uint64_t m_savedFlags = 0;
};

/// What a BigThrower is given (`BigThrowerGoal_Init`): his flag, the throws before he tires (0: never), his fatigue
/// and damage, and the eight object types.
struct BigThrowerOrder {
    double flag = 0;
    int cycles = 0;
    int fatigue = 5;
    int damage = 30;
    std::array<std::uint16_t, 8> objects{};
};

/// The BigThrower goal (type `0x87`, Vargas in stage 2): a taunt, then from his flag: pick an object up, aim at his
/// enemy and throw it; after `cycles` throws he tires.
/// @orig 0x002ed0a8 BigThrowerGoal_Init (unknown)
class BigThrowerGoal final : public Goal {
  public:
    /// The states (`+0x18`): taunt, to the flag, pick up, aim, throw.
    enum class State : std::uint8_t { Taunt = 0, ToFlag = 1, PickUp = 2, Aim = 3, Throw = 4 };

    BigThrowerGoal(const BigThrowerOrder& order, ScriptServices* services, const FlagServices* flags);

    /// As the BigBrawler's Start.
    /// @orig 0x002ed168 BigThrowerGoal_Start (unknown)
    void start(Brain& brain) override;
    /// One update by state. **Coney stand-ins**: as the BigBrawler's (no object in hand, the throw clip played); the
    /// half-way taunt is line `0x11`; keeping his distance while his hand is busy is not built.
    /// @orig 0x002ed718 BigThrowerGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// As the BigBrawler's End.
    /// @orig 0x002ed260 BigThrowerGoal_End (unknown)
    void end(Brain& brain) override;

    [[nodiscard]] State state() const { return m_state; }
    /// Throws since he last tired.
    [[nodiscard]] int throws() const { return m_throws; }

  private:
    BigThrowerOrder m_order;
    ScriptServices* m_services;
    const FlagServices* m_flags;
    State m_state = State::Taunt;
    int m_throws = 0;
    int m_misses = 0;
    bool m_halfTaunted = false;
    std::uint64_t m_waitUntilMs = 0;
    int m_savedTurnBoost = 0;
    std::uint64_t m_savedFlags = 0;
};

} // namespace coney::ai
