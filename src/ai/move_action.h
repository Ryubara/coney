// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "ai/action.h"
#include "ai/route_follower.h"
#include "animation/anim_math.h"
#include "human/locomotion.h"

// The move action: takes the human to a point at a gait, straight when the line there is walkable, else along a
// route from the planner. It writes no stick: each update it sets the brain's heading and speed (brain `+0x110`,
// `+0x114`), slowing for the corners ahead so the human stays on the walkable polygons, turning on the spot when it
// stands far off its way, and giving up when it is stuck.
// Research: docs/research/ai.md#move-action, docs/research/ai.md#route-follow

namespace coney::ai {

/// Within this distance of its point a move has nothing to do (Start).
inline constexpr float kMoveNothingToDo = 0.1F;
/// The final point is looked for in a straight line again every this many updates, offset by the brain's slot.
inline constexpr std::uint32_t kStraightCheckSteps = 30;
/// A move ends within this distance in plan of its point when the heights differ by more than kMoveHeightGap.
inline constexpr float kMoveHeightReach = 1.0F;
inline constexpr float kMoveHeightGap = 1.5F;
/// Standing more than this off its way (radians, 30°), a move turns on the spot first.
inline constexpr float kTurnOnSpotAngle = 0.5236F;
/// A human moving slower than this stands, for the turn on the spot (**Coney choice**, m/s).
inline constexpr float kStandingSpeed = 0.05F;
/// The stuck test: every this many updates of moving, less than kStuckDistance covered fails the move.
inline constexpr std::uint32_t kStuckSteps = 60;
inline constexpr float kStuckDistance = 0.2F;
/// The corner speed looks at corners farther than this from the human.
inline constexpr float kCornerMinDistance = 0.35F;
/// The corner speed's trial falls by 1 + 0.75 × human `+0x333` m/s for each failed trial (**Coney choice**: Coney
/// has no `+0x333`, so it falls by 1).
inline constexpr float kCornerSpeedStep = 1.0F;
/// The braking distance is the distance covered in this long at the first corner's speed (**Coney choice**, s).
inline constexpr float kBrakingSeconds = 0.5F;

/// What a move is asked to do (`MoveAction_Init`'s arguments).
struct MoveRequest {
    anim::Vec3 point;         ///< Where to go.
    float radius = 0.0F;      ///< It arrives within this distance in plan.
    int gait = 2;             ///< The gait (1 sneak, 2 walk, 3 jog, 4 run, 5 sprint): its speed is the most it goes.
    bool option = false;      ///< The option bit; kept, not read (its meaning is not traced).
    std::int16_t delayMs = 0; ///< Its start delay.
    bool flagKind = false;    ///< The "flag kind `0x12`" bit; kept, not read.
};

/// The speed of gait `gait` for a human of speeds `speeds` (`Human_SpeedForGait`): 1 sneak, 2 walk, 3 jog, 4 run, 5
/// sprint; any other value walks.
/// @orig 0x0022ae40 Human_SpeedForGait (unknown)
[[nodiscard]] float gaitSpeed(const human::Speeds& speeds, int gait);

/// The move action.
/// @orig 0x002fb9e8 MoveAction_Init (unknown)
class MoveAction final : public Action {
  public:
    /// A move as `request` asks.
    explicit MoveAction(const MoveRequest& request) : Action(request.delayMs), m_request(request) {}

    /// Done at once within kMoveNothingToDo of the point. Otherwise the brain's `+0x284` is cleared (**Coney
    /// choice**), the aim is written to the brain and a route is asked for: none needed, it goes straight; none
    /// possible, it is done with the brain's `+0x284` set.
    /// @orig 0x002fc420 MoveAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// One update: waits while the human is busy; every kStraightCheckSteps updates drops a route whose point is in
    /// a straight line; done within the radius (or within kMoveHeightReach in plan but kMoveHeightGap apart in
    /// height); aims at the route's waypoint or the point; turns on the spot while standing more than 30° off; else
    /// goes at the corner speed; done (`+0x284` = 3) when stuck. **Coney choice**: no line of sight is tested
    /// (Coney has none; the original's every-31-updates check that the point is still in sight is not made, and the
    /// straight check uses the walkable line), and the dynamic obstacles and the steering round humans are not built.
    /// @orig 0x002fc5c0 MoveAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Stops the move; never refused.
    [[nodiscard]] bool abort(Brain& brain) override;

    /// What it was asked.
    [[nodiscard]] const MoveRequest& request() const { return m_request; }
    /// Whether it follows a route now.
    [[nodiscard]] bool routed() const { return m_follower.has_value(); }

  private:
    // The speed for the corners ahead: the first and second corner's trial speeds and the braking distance, worked
    // out again whenever the waypoint changes; the first's beyond the braking distance from the waypoint, within it
    // the slower of the two (**Coney choice**: the original's reads the second's there, which could speed a human up
    // into the first corner when the second is gentler).
    // @orig 0x002fc158 MoveAction_CornerSpeed (unknown)
    [[nodiscard]] float cornerSpeed(Brain& brain, anim::Vec3 position);
    // The fastest of the trial speeds, from `top` down by kCornerSpeedStep to `floor`, at which a human coming into
    // the corner at `corner` along `in` and turning to `out` at its gait's turn rate stays on the polygons.
    // @orig 0x002fbd18 MoveAction_CornerTrial (unknown)
    [[nodiscard]] static float cornerTrial(const Brain& brain, anim::Vec3 corner, anim::Vec3 in, anim::Vec3 out,
                                           float top, float floor);
    // The stuck test, once per update of moving.
    // @orig 0x002fc330 MoveAction_Stuck (unknown)
    [[nodiscard]] bool stuck(anim::Vec3 position);
    // Ends the move: the human stands.
    [[nodiscard]] static ActionStatus finish(Brain& brain);

    MoveRequest m_request;
    std::optional<RouteFollower> m_follower;
    std::uint32_t m_updates = 0;
    // The corner speeds for the waypoint index they were worked out at.
    std::optional<std::size_t> m_cornersAt;
    float m_firstCornerSpeed = 0.0F;  // +0x0c
    float m_secondCornerSpeed = 0.0F; // +0x44
    float m_brakingSquared = 0.0F;    // +0x48
    // The stuck test's window.
    anim::Vec3 m_stuckFrom;
    std::uint32_t m_movingSteps = 0;
};

} // namespace coney::ai
