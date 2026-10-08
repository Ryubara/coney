// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/enemy_scan.h"
#include "ai/goal.h"
#include "ai/scout_services.h"
#include "ai/story_tactics.h"
#include "animation/anim_math.h"

// TacticScout: posted guards who scan for enemies every 500 ms, fight one they spot or who hits them, and send one of
// them running to a phone to call the gang; the call gives the enemy's gang its second wanted timer, which a level's
// stealth section fails on. Level87's club (checkpoint 5) is built from it.
// Research: docs/research/stealth.md#scouts, docs/research/stealth.md#spotted, docs/research/ai-goals.md#goal-scout,
// docs/research/ai-goals.md#goal-call-gang

namespace coney::ai {

/// The scout's scan interval while at his post, ms (brain `+0x144`).
inline constexpr std::uint64_t kScoutScanMs = 500;
/// How far off his post the scout may stand, and off his heading he may face, before going back.
inline constexpr float kScoutPostSlack = 0.3F;
inline constexpr float kScoutHeadingSlack = 0.2618F; // 15 degrees
/// Within this of a hidden player the scout walks back to his post at gait 2 whatever the distance.
inline constexpr float kScoutHiddenNear = 10.0F;
/// The caller's run (gait 5), how near the phone counts as there, and how long the call takes once there.
inline constexpr int kCallGait = 5;
inline constexpr float kCallReach = 1.0F;
inline constexpr std::uint64_t kCallMs = 1500;
/// The tactic's member check, ms.
inline constexpr std::uint64_t kScoutTacticCheckMs = 200;

/// The Scout goal (type `0x6f`): the guard at his post. Resumed, his threat response is 0 (his think pushes no fight of
/// its own) and his scan interval 500 ms (`schedule`); both come back at End. Each update off his post by more than
/// 0.3 m he walks back (gait 2 within `roamRadius`, else gait 3; gait 2 whenever the hidden player is within 10 m),
/// then turns to his heading when more than 15° off. **Coney stand-ins**: the radar blip, the dropped object, the
/// look and fidget clips, the head glances and the roaming within the radius and arc are not built.
/// @orig 0x002d6ca0 ScoutGoal_Init (unknown)
class ScoutGoal final : public Goal {
  public:
    /// A post at `post` facing `heading` (radians), roaming within `roamRadius`, scanning through `schedule` (which
    /// must outlive the goal), the hidden player looked for through `player` (may be null).
    ScoutGoal(anim::Vec3 post, float heading, float roamRadius, ScanSchedule& schedule, const Brain* player)
        : Goal(GoalType::Scout), m_post(post), m_heading(heading), m_roamRadius(roamRadius), m_schedule(&schedule),
          m_player(player) {}
    /// @orig 0x002d6db0 ScoutGoal_Start (unknown)
    void start(Brain& brain) override;
    /// @orig 0x002d6e10 ScoutGoal_Resume (unknown)
    void resume(Brain& brain) override;
    /// @orig 0x002d6ef0 ScoutGoal_Suspend (unknown)
    void suspend(Brain& brain) override;
    /// @orig 0x002d6f98 ScoutGoal_End (unknown)
    void end(Brain& brain) override;
    /// @orig 0x002d73b8 ScoutGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// His post.
    [[nodiscard]] anim::Vec3 post() const { return m_post; }

  private:
    // The post's settings on, or the old ones back.
    void apply(Brain& brain);
    void restore(Brain& brain);

    anim::Vec3 m_post;
    float m_heading;
    float m_roamRadius;
    ScanSchedule* m_schedule;
    const Brain* m_player;
    int m_oldThreatResponse = 0;
    bool m_applied = false;
};

/// What a CallGang goal is told.
struct CallOrder {
    int gang = -1;        ///< The gang the call makes wanted (the enemy's).
    float radius = 10.0F; ///< The phone search radius.
    int count = 0;        ///< Responders to queue.
    int delaySeconds = 0; ///< Their delay.
};

/// The CallGang goal (type `0x6e`): the caller runs (gait 5) to the farthest phone within the radius, or calls where
/// he stands with none; there, after the call's length, the responders are queued and the enemy's gang gets its second
/// wanted timer; then done. Killing or downing him first ends the goal without a call. **Coney stand-ins**: the lines
/// (`0xa9`, `0x16`), the call clips (`0x29c`, `0x29d`), the spinning icon, the radar blip and the tutorial hint are not
/// built; a call spot beside him is where he stands.
/// @orig 0x002d5cf0 CallGangGoal_Init (unknown)
class CallGangGoal final : public Goal {
  public:
    /// A call as `order` says through `services` (which must outlive the goal).
    CallGangGoal(const CallOrder& order, ScoutServices& services)
        : Goal(GoalType::CallGang), m_order(order), m_services(&services) {}
    /// Picks the phone and marks the caller active.
    /// @orig 0x002d5db8 CallGangGoal_Start (unknown)
    void start(Brain& brain) override;
    /// The caller is no longer active.
    /// @orig 0x002d5f98 CallGangGoal_End (unknown)
    void end(Brain& brain) override;
    /// @orig 0x002d6638 CallGangGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Whether the call was made.
    [[nodiscard]] bool called() const { return m_called; }
    /// Where he calls from.
    [[nodiscard]] anim::Vec3 phone() const { return m_phone; }

  private:
    CallOrder m_order;
    ScoutServices* m_services;
    anim::Vec3 m_phone;
    std::uint64_t m_arrivedMs = 0;
    bool m_arrived = false;
    bool m_called = false;
};

/// TacticScout (`0x27`): `call`'s count and count2 are the call's responders and delay, its range the phone search
/// radius (twice the caller's far melee range when negative), range2 the roam radius. Start gives each member who can
/// act a Scout goal at where he stands, facing his facing. Each update the members at their posts scan
/// (ScanSchedule, 500 ms); every 200 ms a member with an enemy and no fight is given one. A member hit, warned of an
/// attack or seeing a new enemy (events 1, `0x10`, `0xb`) while at his post fights him and, when `scout` allows it
/// (a responder spawner, the enemy's gang not yet called on, no other caller), runs to call the gang. **Coney
/// stand-ins**: the help call to members within 15 m, the investigation of a hit from someone unseen and the gang's
/// alert state are not built.
/// @orig 0x0031a430 ScoutTactic_Construct (unknown)
class ScoutTactic final : public StoryTactic {
  public:
    /// The tactic of `call`, calling through `services.scout` (none: no call is ever made).
    ScoutTactic(const script::TacticCall& call, const TacticServices& services) : StoryTactic(call, services) {}
    /// @orig 0x0031af98 ScoutTactic_Start (unknown)
    void start(Gang& gang) override;
    /// @orig 0x0031b030 ScoutTactic_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// @orig 0x0031a818 ScoutTactic_OnEvent (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;
    /// The members' scan schedule.
    [[nodiscard]] ScanSchedule& schedule() { return m_schedule; }

  private:
    // Pushes the call on `member` when the level allows it.
    void maybeCall(Brain& member, const Brain& enemy);

    ScanSchedule m_schedule;
    std::uint64_t m_nextCheckMs = 0;
};

} // namespace coney::ai
