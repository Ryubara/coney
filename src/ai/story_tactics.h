// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ai/tactic.h"
#include "scripting/story_bindings.h"

// The gang tactics the story's missions set with their `Tactic<Name>` bindings (TacticAttack and TacticConfront are
// ai/tactic_attack.h and ai/tactic_confront.h): moving as a group behind the leader (MoveToFlag, TravelPath,
// WalkinTall, Wander), keeping a place (HanginOut, Idle, UseFlag and the kinds whose goals are not traced), defending a
// human, holding a line and pursuing another gang. Each is built from the binding's arguments (script::TacticCall) and
// what the level gives the brains (TacticServices). The members' own goals of most kinds are not traced, so each class
// says where it stands in for them.
// Research: docs/research/ai.md#tactic-kinds

namespace coney::ai {

class Brain;
class FlagServices;
class Formations;
class Gang;
class ScriptServices;

/// What the story tactics ask of the level: the flags, the scripts (and through them the humans by handle and player
/// 1), and the formations the followers join. All must outlive the tactic.
struct TacticServices {
    FlagServices* flags = nullptr;
    ScriptServices* scripts = nullptr;
    Formations* formations = nullptr;
};

/// The tactic codes the story tactics return or fire (`TacticGetString`, docs/references/bindings/ai.md).
inline constexpr int kTacTimeUp = 1;
inline constexpr int kTacSeeEnemy = 3;
inline constexpr int kTacSeePlayer = 4;
inline constexpr int kTacDamage = 5;
inline constexpr int kTacAttacked = 6;
inline constexpr int kTacInRange = 7;
inline constexpr int kTacArrived = 8;
inline constexpr int kTacHumanGone = 11;
inline constexpr int kTacAnimDone = 15;

/// The base of the story tactics: the binding's arguments, the level's services, and the human events each kind
/// answers with a callback (1 damage → 5, 10 saw the player → 4, 11 an enemy spotted → 3, 16 attacked → 6).
class StoryTactic : public Tactic {
  public:
    /// A tactic of `call`'s kind with `services`.
    StoryTactic(const script::TacticCall& call, const TacticServices& services);
    /// The mapped events fire their codes; none is used.
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

  protected:
    /// The binding's arguments.
    [[nodiscard]] const script::TacticCall& call() const { return m_call; }
    /// The level's services.
    [[nodiscard]] const TacticServices& services() const { return m_services; }
    /// Whether event `id` fires its code for this kind.
    [[nodiscard]] virtual bool answers(int id) const;

  private:
    script::TacticCall m_call;
    TacticServices m_services;
};

/// MoveToFlag (`0x19`), TravelPath (`0x17`), WalkinTall (`0x15`) and Wander (`0x16`): the leader (Gang::leader()) is
/// given the moving goal, every other AI member follows him (a TrackHumanGoal at 3, 1, 0.75 and 4 m, **Coney stand-in**
/// for `Goal_FollowPlayer` in the leader's formation). MoveToFlag and WalkinTall walk to the flag (WalkinTall at a
/// walk) and fire 8 once the leader arrived; TravelPath walks `path` (mode 1 looping, else 2), and with no path the
/// leader stands (**stand-in** for `TravelFlagNet`); Wander's leader stands (**stand-in**: the wander goal is not
/// traced). WalkinTall returns 7 while a member is within `range` of a hostile (checked each second). Banter is not
/// built.
/// @orig 0x00316298 Tactic_MoveToFlag (unknown)
class GroupMoveTactic final : public StoryTactic {
  public:
    /// The group move of `call`, `path` the points of its path (TravelPath only).
    GroupMoveTactic(const script::TacticCall& call, std::vector<double> path, const TacticServices& services)
        : StoryTactic(call, services), m_path(std::move(path)) {}
    void start(Gang& gang) override;
    [[nodiscard]] int update(Gang& gang) override;

  private:
    double m_leader = 0;
    bool m_moving = false;
    std::uint64_t m_nextCheckMs = 0;
    std::vector<double> m_path;
};

/// The kinds whose members keep a place: HanginOut (`0x18`) walks them to the flag (within `range`) and, without full
/// awareness, narrows their view by 20° and their sight to 75 %; UseFlag (`0x21`) walks them to the flag (gait 3,
/// within `range`) and, once player 1 comes within `range` of it, sends them off and returns 7; Idle (`0x24`) holds
/// them where they stand and, with `dynIdle`, ends their dynamic idles on events 1, 11 and 16 and returns 15 once none
/// is left; Scout (`0x27`) melees with members that have an enemy (each 200 ms) and with one hit, attacked or spotting;
/// ManWeaponPile (`0x06`), Vandalize (`0x1c`), Steal (`0x1d`) and AvoidEnemies (`0x20`) stand. **Coney stand-ins**:
/// the hang-out, use-flag, man-the-pile, destroy, steal, avoid and scout goals are not traced, so the members stand
/// where those walks leave them; banter, answering violence and the anim substitutions are not built.
/// @orig 0x00315fc8 Tactic_HanginOut (unknown)
/// @orig 0x00316ee8 Tactic_Idle (unknown)
class StationTactic final : public StoryTactic {
  public:
    StationTactic(const script::TacticCall& call, const TacticServices& services) : StoryTactic(call, services) {}
    void start(Gang& gang) override;
    [[nodiscard]] int update(Gang& gang) override;
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

  protected:
    [[nodiscard]] bool answers(int id) const override;

  private:
    bool m_playerNear = false;
    bool m_brokenOff = false;
    std::uint64_t m_nextCheckMs = 0;
};

/// Defend (`0x02`): the AI members keep round the human (a TrackHumanGoal at `range`, **Coney stand-in** for
/// `FollowAndDefend`); 11 once the human is gone or out of health, else 9 when no member has an enemy (each 1.5 s).
/// @orig 0x00315d98 Tactic_Defend (unknown)
class DefendTactic final : public StoryTactic {
  public:
    DefendTactic(const script::TacticCall& call, const TacticServices& services) : StoryTactic(call, services) {}
    void start(Gang& gang) override;
    [[nodiscard]] int update(Gang& gang) override;
    /// The defended human's handle (the binding's `human`).
    [[nodiscard]] double defended() const { return call().flags.at(0); }

  private:
    std::uint64_t m_nextCheckMs = 0;
};

/// HoldTheLine (`0x04`): min(the line's length in metres, 60 % of the members) defenders walk to the line's two flags
/// in turn (**Coney stand-in** for the even spacing of `HTLDefense`), the others to the third flag; 9 when no member
/// has an enemy (each 1.75 s). The crossing (12) and hits (14) are not built.
/// @orig 0x00313e30 Tactic_HoldTheLine (unknown)
class HoldTheLineTactic final : public StoryTactic {
  public:
    HoldTheLineTactic(const script::TacticCall& call, const TacticServices& services) : StoryTactic(call, services) {}
    void start(Gang& gang) override;
    [[nodiscard]] int update(Gang& gang) override;

  private:
    std::uint64_t m_nextCheckMs = 0;
};

/// Pursue (`0x14`): the members take the target gang's leader as an enemy and melee (**Coney stand-in** for `Chase`);
/// each 150 ms, 9 when the target gang is gone, empty or leaderless, else 7 while a member is within `range` of one
/// of its living members (who becomes his enemy). With target gang -1 the first member's target's gang is taken.
/// @orig 0x00315ee8 Tactic_Pursue (unknown)
class PursueTactic final : public StoryTactic {
  public:
    PursueTactic(const script::TacticCall& call, const TacticServices& services) : StoryTactic(call, services) {}
    void start(Gang& gang) override;
    [[nodiscard]] int update(Gang& gang) override;

  private:
    int m_target = -1;
    std::uint64_t m_nextCheckMs = 0;
};

/// The tactic of `call`'s kind (TravelPath walking `path`); null for Attack and Confront, which are made elsewhere.
[[nodiscard]] std::unique_ptr<Tactic> makeStoryTactic(const script::TacticCall& call, std::vector<double> path,
                                                      const TacticServices& services);

} // namespace coney::ai
