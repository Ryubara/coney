// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"

// GoalTrackHuman: the human joins its target's formation and keeps to its slot: beyond a distance from the slot's point
// it walks there, otherwise it turns to face the target when more than 15° off. Without a slot its point is where it
// stands, so it never walks and only turns. It ends when the target is gone.
// Research: docs/research/ai.md#formations

namespace coney::ai {

class Formations;
class ScriptServices;

/// A tracker clears its actions every this many brain updates; a failed move waits this many idle updates before
/// another.
inline constexpr std::uint64_t kTrackClearPeriod = 40;
inline constexpr int kTrackRetryUpdates = 31;
/// It turns to the target when more than this off it (radians, 15°).
inline constexpr float kTrackTurnAngle = 0.2617994F;
/// Its moves walk (gait 2).
inline constexpr int kTrackGait = 2;

/// The track-human goal (type `0x30`, vtable `0x00541d10`).
/// @orig 0x002df250 TrackHumanGoal_Init (unknown)
class TrackHumanGoal final : public Goal {
  public:
    /// Tracks the human with handle `target`, found through `services`, in `formations` (both must outlive it), keeping
    /// within `distance` of its point.
    TrackHumanGoal(ScriptServices& services, Formations& formations, double target, float distance)
        : Goal(GoalType::TrackHuman), m_services(&services), m_formations(&formations), m_target(target),
          m_distance(distance) {}

    /// Joins the target's formation (made on first use). **Coney choice**: the fight stance it turns on is not built.
    /// @orig 0x002df288 TrackHumanGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Done when the target is gone; every 40 updates clears the actions; with none queued: counts a failed move,
    /// and beyond the distance from its point (its slot's, or where it stands without one) walks there unless a failed
    /// move is still being waited out (31 idle updates); otherwise turns to the target when more than 15° off.
    /// **Coney choices**: the head look every 30 updates is not built, and the move does not face the target (the
    /// move's object to face is not built).
    /// @orig 0x002df3c0 TrackHumanGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Leaves the formation.
    void end(Brain& brain) override;

    /// The target's handle and the distance.
    [[nodiscard]] double target() const { return m_target; }
    [[nodiscard]] float distance() const { return m_distance; }

  private:
    ScriptServices* m_services;
    Formations* m_formations;
    double m_target;  // +0x10
    float m_distance; // +0x14
    int m_failed = 0; // +0x18
};

/// What `GoalTrackHuman(human, target, distance)` does for `brain`: pushes the goal.
/// @orig 0x002df1a8 Goal_TrackHuman (unknown)
bool goalTrackHuman(Brain& brain, ScriptServices& services, Formations& formations, double target, float distance);

} // namespace coney::ai
