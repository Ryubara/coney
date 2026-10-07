// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/story_tactics.h"

// `TacticBossScenarioA`: the Diego and Vargas fight (level 5, mission 7). Each Hurricane gets a goal by character
// (Diego a BigBrawler, Vargas a BigThrower in stage 2 and a BigBrawler in stage 3, the others StationaryThrowers);
// the tactic caps Diego's health per stage until he is tired, starts the break that ends the stage, and answers the
// script with 18 while the break's clip plays and 1 once the stage is over.
// Research: docs/research/ai.md#boss-diego-vargas, docs/research/ai-code.md#t1-boss-diego-vargas

namespace coney::ai {

class Brain;
class TiredGoal;

/// The tactic code the break answers while its clip plays (`TacAnimStart`).
inline constexpr int kTacAnimStart = 18;

/// The Diego and Vargas tactic (type `0x0a`).
/// @orig 0x00309b60 BossDiegoVargasTactic_Init (unknown)
class BossDiegoVargasTactic final : public StoryTactic {
  public:
    BossDiegoVargasTactic(const script::TacticCall& call, const TacticServices& services);

    /// Gives every member that can act his goal (`BossDiegoVargasTactic_GiveGoals`). **Coney stand-in**: the gang's
    /// combat fidget and taunt clips are kept (the substitute group `0x29c` is not built).
    /// @orig 0x0030a6c0 BossDiegoVargasTactic_Start (unknown)
    void start(Gang& gang) override;
    /// The health check: 18 when a break starts, 1 once Diego's break is done; in stage 3, 1 once neither boss stands.
    /// **Coney choice**: each code is answered once, where the original answers 18 on every update the break's clip
    /// plays and 1 on every update after; **Coney stand-in**: a member left without his goal (respawned, events 19 and
    /// 22) gets his again here.
    /// @orig 0x0030a738 BossDiegoVargasTactic_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// Hits (1) and attack warnings (16) go to the member's BigBrawler goal and are used; nothing else is.
    /// @orig 0x0030a798 BossDiegoVargasTactic_OnEvent (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

    /// The stage (`+0x68`).
    [[nodiscard]] int stage() const { return m_stage; }
    /// Vargas's breaks so far in stage 3 (`+0x69`).
    [[nodiscard]] int breaks() const { return m_breaks; }

  private:
    // Gives `member` his goal by character and stage (`BossDiegoVargasTactic_AssignGoal`).
    // @orig 0x00309ea0 BossDiegoVargasTactic_AssignGoal (unknown)
    void assignGoal(Brain& member);
    // The health caps and the break for one boss; the code the tactic answers (0 for none).
    // @orig 0x0030a150 BossDiegoVargasTactic_CheckHealth (unknown)
    int checkHealth(Brain& boss);
    // Whether a boss still stands (stage 3; true in the others).
    // @orig 0x0030a458 BossDiegoVargasTactic_IsAnyBossStanding (unknown)
    [[nodiscard]] bool anyBossStanding(const Gang& gang) const;

    int m_stage;
    int m_breaks = 0;
    bool m_finished = false; // 1 answered (**Coney choice**: once, as 18 is once per break)
};

/// The tired goal on top of `brain`'s stack; null when none.
[[nodiscard]] TiredGoal* tiredGoalOf(Brain& brain);

} // namespace coney::ai
