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
/// A climb leg's climb is tried for this many updates before the move gives up (route state `+0x13`).
inline constexpr std::uint32_t kClimbTries = 31;
/// The corner speed's trial falls by 1 + 0.75 × human `+0x333` m/s for each failed trial (**Coney choice**: Coney
/// has no `+0x333`, so it falls by 1).
inline constexpr float kCornerSpeedStep = 1.0F;
/// The braking distance is the distance covered in this long at the first corner's speed (**Coney choice**, s).
inline constexpr float kBrakingSeconds = 0.5F;

/// A jump leg (docs/research/ai.md#route-jump): the fastest a drop or a jump crosses in plan (m/s), half the fall's
/// gravity, the jump's first and last vertical speed and its step (m/s), and how near (3D) its waypoint a landing
/// keeps the route.
inline constexpr float kJumpMaxAcross = 10.0F;
inline constexpr float kHalfGravity = 7.84F;
inline constexpr float kJumpFirstUp = 0.5F;
inline constexpr float kJumpLastUp = 5.5F;
inline constexpr float kJumpUpStep = 0.75F;
inline constexpr float kLandingKeepsRoute = 1.5F;

/// The larger real root of a t² + b t + c = 0, or 0 when there is none (a linear equation's root when `a` is 0).
/// @orig 0x003378b0 Math_LargerRoot (unknown)
[[nodiscard]] float largerRoot(float a, float b, float c);

/// The launch velocity of an AI's route jump across `way` (the landing point less the take-off point): the first
/// vertical speed from kJumpFirstUp in steps of kJumpUpStep whose arc comes down to `way`'s height at no more than
/// kJumpMaxAcross in plan, and the plan speed that lands it on the point; nothing past kJumpLastUp.
/// @orig 0x0029ade0 Route_JumpArc (unknown)
[[nodiscard]] std::optional<anim::Vec3> routeJumpVelocity(anim::Vec3 way);

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
    // A route leg that is not walked: a climb (kinds 8 and 0x80), or a refused one (the avoid bit, but for a charge).
    // Returns the action's status when the leg decided this update, nothing when the move goes on as a walk.
    // @orig 0x0029b848 Route_ClimbLeg (unknown)
    [[nodiscard]] std::optional<ActionStatus> followLeg(Brain& brain, anim::Vec3 position, anim::Vec3 aim);
    // A jump leg's handler, on every update while the leg is new: aims at the landing point and turns the body to it,
    // then drops off the edge at the speed that lands on the point when that is under kJumpMaxAcross (the point lower
    // and the leg not avoided), else jumps; a refused jump ends the move.
    // @orig 0x0029baa8 Route_JumpLeg (unknown)
    [[nodiscard]] ActionStatus jumpLeg(Brain& brain, anim::Vec3 position, anim::Vec3 aim);
    // The update the human lands during the move (`Human_Land`'s part for a move action): near the jump leg's
    // waypoint the route goes on (past it when the arc reached it); otherwise the move ends and is planned again.
    // @orig 0x0023e090 Human_Land (unknown)
    [[nodiscard]] std::optional<ActionStatus> landed(Brain& brain, anim::Vec3 position);
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
    // A climb leg: the waypoint index it is for, the failed tries there, and whether the human climbed last update.
    std::size_t m_climbIndex = 0;
    std::uint32_t m_climbFails = 0;
    bool m_wasClimbing = false;
    // A jump leg: what the handler did (route state 1 a jump, 2 a drop), the human's landings counted so far, the
    // launch's speed in plan, and whether the landing waypoint was reached.
    enum class JumpState : std::uint8_t { None, Drop, Jumped };
    JumpState m_jump = JumpState::None;
    std::uint32_t m_landings = 0;
    float m_launchAcross = 0.0F;
    bool m_airArrived = false;
    std::size_t m_jumpIndex = 0; // the waypoint index m_jump is for
};

} // namespace coney::ai
