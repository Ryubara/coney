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
/// A move's arrival is measured in 3D, and in plan once the 3D distance is this or less (`0x0023f9d0`), m.
inline constexpr float kArrivalPlanWithin = 1.0F;
/// A move ends within this distance in plan of its point when the heights differ by more than kMoveHeightGap.
inline constexpr float kMoveHeightReach = 1.0F;
inline constexpr float kMoveHeightGap = 1.5F;
/// Standing more than this off its way (radians, 30°), a move turns on the spot first.
inline constexpr float kTurnOnSpotAngle = 0.5236F;
/// A human moving slower than this stands, for the turn on the spot (**Coney choice**, m/s).
inline constexpr float kStandingSpeed = 0.05F;
/// The stuck test: on the update after this many of moving, less than kStuckDistance covered (in 3D) fails the move.
inline constexpr std::uint32_t kStuckSteps = 60;
inline constexpr float kStuckDistance = 0.2F;
/// The corner speed looks at corners farther than this from the human.
inline constexpr float kCornerMinDistance = 0.35F;
/// A climb leg's climb is tried for this many updates before the move gives up (route state `+0x13`).
inline constexpr std::uint32_t kClimbTries = 31;
/// A fast climber above the jog tries the climb of the leg after its current waypoint within this distance (3D) of
/// that leg's far waypoint (`Route_TryClimbNear`, docs/research/ai.md#route-follow).
inline constexpr float kEarlyClimbReach = 4.5F;
/// The corner speed's trial falls by 1 + 0.75 × human `+0x333` m/s for each failed trial (**Coney choice**: Coney
/// has no `+0x333`, so it falls by 1).
inline constexpr float kCornerSpeedStep = 1.0F;
/// The braking distance is the distance covered in this long at the first corner's speed (**Coney choice**, s).
inline constexpr float kBrakingSeconds = 0.5F;
/// A new point this near the old one (3D, m; squared 0.005) leaves a move as it is (`MoveAction_SetTarget`).
inline constexpr float kRetargetIgnore = 0.0707F;

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
/// sprint; 0 and any other value give 0 (docs/research/characters.md).
/// @orig 0x0022ae40 Human_SpeedForGait (unknown)
[[nodiscard]] float gaitSpeed(const human::Speeds& speeds, int gait);

/// Whether a human tries the climb of the next route leg now, before it reaches the current waypoint (the fast
/// climber's early climb): the leg out of the current waypoint is a climb (kind 8 or `0x80`), the human has flag `0x2`
/// (`HuSetFastClimber`), its gait (human `+0x1a8`) is above the jog, and it is within kEarlyClimbReach (3D) of the
/// climb's far waypoint, `distance` away (docs/research/ai.md#route-follow).
/// @orig 0x0029b9b0 Route_TryClimbNear (unknown)
[[nodiscard]] bool earlyClimbDue(std::uint16_t nextLegKind, bool fastClimber, human::Gait gait, float distance);

/// The move action.
/// @orig 0x002fb9e8 MoveAction_Init (unknown)
class MoveAction final : public Action {
  public:
    /// A move as `request` asks.
    explicit MoveAction(const MoveRequest& request) : Action(request.delayMs), m_request(request) {}

    /// Done at once within kMoveNothingToDo of the point. Otherwise the brain's `+0x284` is cleared (**Coney
    /// choice**), the steering's detour is reset, the aim is written to the brain and a route is asked for: none
    /// needed, it goes straight; none possible, it is done with the brain's `+0x284` set.
    /// @orig 0x002fc420 MoveAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// One update: done when aborted (retarget()); waits while the human is busy; every kStraightCheckSteps updates
    /// drops a route whose point is in a straight line; done within the radius, in 3D (in plan within
    /// kArrivalPlanWithin), or within kMoveHeightReach in plan but kMoveHeightGap apart in height; aims at the route's
    /// waypoint or the point; turns on the spot while standing more than 30° off; else goes at the corner speed (with
    /// no route, turnLimit() onto the point); done (`+0x284` = 3) when stuck. **Coney choice**: no line of sight is
    /// tested (Coney has none; the original's every-31-updates check that the point is still in sight is not made, and
    /// the straight check uses the walkable line), the dynamic obstacles are not built, and a route's climb and jump
    /// legs are not steered round humans (their aim is the leg's).
    /// @orig 0x002fc5c0 MoveAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Stops the move; never refused.
    [[nodiscard]] bool abort(Brain& brain) override;

    /// What it was asked.
    [[nodiscard]] const MoveRequest& request() const { return m_request; }
    /// A speed in place of the gait's top (m/s, action `+0x40`), as a goal pacing the human sets it.
    /// @orig 0x0029f710 Brain_SetMoveSpeed (unknown)
    void setSpeed(float speed) { m_speed = speed; }
    /// The speed set in place of the gait's top; nothing when it goes at its gait's.
    [[nodiscard]] std::optional<float> speed() const { return m_speed; }
    /// A new arrival radius for the running move (FollowFormation updates its move's).
    void setRadius(float radius) { m_request.radius = radius; }
    /// Gives the move a new point (docs/research/ai.md#follow-retarget, item 3): nothing for one within
    /// kRetargetIgnore of the old (3D). Otherwise the point is stored; a straight move goes on to it, and a route is
    /// kept, its destination moved, when its last node has a walkable line to the new point. Else, unless the human
    /// is in a jump, a drop or a kind-8 climb, the route goes and the move starts again on the next update (a straight
    /// line or a new route to the point); in one of those the move ends on its next update (the clips play on) and its
    /// goal makes a new one.
    /// @orig 0x002fbbe8 MoveAction_SetTarget (unknown)
    void retarget(Brain& brain, anim::Vec3 point);
    /// Whether it follows a route now.
    [[nodiscard]] bool routed() const { return m_follower.has_value(); }
    /// The route it follows; null when it goes straight.
    [[nodiscard]] const RouteFollower* follower() const { return m_follower ? &*m_follower : nullptr; }

  private:
    // The speed for the corners ahead, at most `top` (the move's speed this update): the first and second corner's
    // trial speeds and the braking distance, worked out again whenever the waypoint changes; the first's beyond the
    // braking distance from the waypoint, within it the slower of the two (**Coney choice**: the original's reads the
    // second's there, which could speed a human up into the first corner when the second is gentler).
    // @orig 0x002fc158 MoveAction_CornerSpeed (unknown)
    [[nodiscard]] float cornerSpeed(Brain& brain, anim::Vec3 position, float top);
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
    // @orig 0x002fc330 MoveAction_CheckStuck (unknown)
    [[nodiscard]] bool stuck(anim::Vec3 position);
    // Ends the move: the human stands.
    [[nodiscard]] static ActionStatus finish(Brain& brain);

    // Whether the human is in a jump, a drop or a kind-8 climb of its route (route states 1, 2 and 3).
    [[nodiscard]] bool inRouteLeg(const Brain& brain) const;
    // The fastest speed, at most `top` and at least the walk, at which the human at `position` turns onto the circle
    // tangent to his facing that runs through `to` (the corner speed's first limit, used for a move with no route).
    // @orig 0x002fcd90 Move_CornerSpeedLimit (unknown)
    [[nodiscard]] static float turnLimit(const Brain& brain, anim::Vec3 position, anim::Vec3 to, float top);

    MoveRequest m_request;
    std::optional<float> m_speed;     // +0x40: a speed in place of the gait's top
    bool m_aborted = false;           // the abort bit: the move ends on its next update
    bool m_restartedClimbing = false; // started again mid-climb: that climb's end passes no waypoint of the new route
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
    // Whether the last update asked for the fast climber's early climb: when the human climbs now, the route moves on
    // onto the climb's leg at once.
    bool m_earlyClimb = false;
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
