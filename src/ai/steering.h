// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"

// The steering round humans: each update a move action lets the walker bend its aim round the one human most in the
// way of its next step. A human is a disc of kBlockerRadius; each other human's step is predicted (a player's from
// his facing and speed, an AI's from his brain's aim and speed) and the walker's step, taken relative to his, is swept
// against the disc. By where the blocker stands and which way he heads the walker passes beside him, overtakes him,
// goes round behind him or yields; speed changes go through an override the move reads on its next update. All of it
// works in plan (x, y).
// Research: docs/research/ai.md#steering, docs/research/ai.md#neighbour-sectors (the blocker test)

namespace coney::ai {

class Brain;

} // namespace coney::ai

namespace coney::human {
class Human;
} // namespace coney::human

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

namespace coney::ai {

/// The radius of the disc a human blocks (m; 0.3969 = its square).
inline constexpr float kBlockerRadius = 0.63F;
/// A predicted step is this many seconds of the human's speed.
inline constexpr float kStepSeconds = 0.75F;
/// The most humans a decision lists round the walker.
inline constexpr std::size_t kSteerListMax = 60;
/// The farthest the list reaches (m), whatever the look-ahead.
inline constexpr float kSteerListReach = 10.0F;
/// A move that steers arrives within this of its (detour) aim (m; brain `+0x118`).
inline constexpr float kSteerAimRadius = 0.3F;
/// Within this of its destination (m) a move is not steered.
inline constexpr float kSteerNearDestination = 1.0F;
/// Below this move speed (m/s) decisions count as slow; the 16th slow one in a row looks kSlowLookAheadScale × as far.
inline constexpr float kSlowMoveSpeed = 4.0F;
inline constexpr std::uint8_t kSlowDecisions = 16;
inline constexpr float kSlowLookAheadScale = 10.0F;
/// A player's brain's human moving slower than this (m/s) stands, as a blocker.
inline constexpr float kBlockerStandingSpeed = 0.05F;
/// A walker slower than this (m/s, gait 0) while avoiding keeps heading for the held point.
inline constexpr float kSteerStandingSpeed = 0.5F;
/// A walker without right of way yields to a contact nearer than this (m).
inline constexpr float kYieldContact = 3.0F;
/// The share of a speed kept when following, slowing for a corner or matching a node.
inline constexpr float kSteerSlowShare = 0.75F;
/// The case thresholds: cos 30°, cos 45° and cos 50°.
inline constexpr float kCos30 = 0.8660254F;
inline constexpr float kCos45 = 0.70710677F;
inline constexpr float kCos50 = 0.64278764F;
/// The ground probe under a detour point casts down from this far above it (m), this far (m), and refuses a first hit
/// on a triangle with this flag (the bit the ground snap passes on as "under cover"; what it marks is inferred).
inline constexpr float kGroundProbeLift = 0.1F;
inline constexpr float kGroundProbeLength = 2.0F;
inline constexpr std::uint16_t kGroundProbeFlag = 0x10;

/// A brain's steering state (brain `+0xa0`; `Steering_Reset` `0x00288a78` when made).
struct SteeringState {
    anim::Vec3 heldPoint;           ///< `+0x00`, the point the last detour stored, aimed at between decisions.
    anim::Vec3 contactPoint;        ///< `+0x10`, where the last decision predicted the two would meet.
    Brain* avoiding = nullptr;      ///< `+0x24`, the human being avoided (null: not avoiding).
    std::uint8_t slowDecisions = 0; ///< `+0x29`, slow decisions in a row.
    bool enabled = true;            ///< `+0x2a`.
    std::uint8_t sinceDetour = 0;   ///< `+0x2b`, decisions since the detour was taken.
    std::uint16_t overrideLeft = 0; ///< `+0x32`, updates the speed override still runs.
    float overrideSpeed = 0.0F;     ///< `+0x34`, the speed override.
    float score = -1e9F;            ///< `+0x3c`, the detour's contact fraction; nothing traced reads it.

    /// The speed override while it runs: a move takes it in place of its gait's speed.
    [[nodiscard]] std::optional<float> speedOverride() const {
        return overrideLeft != 0 ? std::optional<float>{overrideSpeed} : std::nullopt;
    }
};

/// Turns avoiding `who` on, or off for null: on raises the brain's turn boost by one, off lowers it again.
/// @orig 0x00288b70 Steering_SetAvoiding (unknown)
void setAvoiding(Brain& brain, Brain* who);
/// Ends a detour at its expiry: avoiding off, the score back to −1e9 and the decisions since the detour to 0.
/// @orig 0x00288ad0 Steering_Clear (unknown)
void clearSteering(Brain& brain);
/// A move's start: the held point cleared, avoiding off, the score −1e9, the decisions since a detour 0, and the slow
/// counter seeded with the human's index, which staggers the humans' first × 10 look-ahead. **Coney choice**: the
/// brain's slot stands for the human's index (human `+0x92`).
/// @orig 0x002890a8 Steering_ResetAvoidance (unknown)
void resetAvoidance(Brain& brain);
/// Sets the speed override to `speed` for the next `updates` updates (a speed of 0 is gait 0's, as
/// `Steering_SetGait` `0x00288c60` gives it).
/// @orig 0x00288cb0 Steering_SetSpeed (unknown)
void setSteeringSpeed(Brain& brain, float speed, std::uint16_t updates);
/// +1 when `point` lies on the side of `origin` that `side` points to (their dot product at least 0), else −1.
/// @orig 0x00288be0 Steering_SideSign (unknown)
[[nodiscard]] float sideSign(anim::Vec3 origin, anim::Vec3 side, anim::Vec3 point);
/// Whether segments `p1`-`p2` and `q1`-`q2` cross in plan (touching does not count).
/// @orig 0x00337308 Segment_Intersect2D (unknown)
[[nodiscard]] bool segmentsCross(anim::Vec3 p1, anim::Vec3 p2, anim::Vec3 q1, anim::Vec3 q2);
/// The radius of the circle through `from` and `to` whose centre lies from `from` along `axis`, in plan: |d| / (2 |d̂ ·
/// axis|), d = `to` − `from`. 0 when the points meet or d is square to the axis.
/// @orig 0x003375d0 Math_CircleRadius2D (unknown)
[[nodiscard]] float cornerRadius(anim::Vec3 from, anim::Vec3 to, anim::Vec3 axis);
/// The speed at most `speed` (the human's own) at which the brain's human can turn onto `point`. R is the radius of
/// the circle tangent to his facing at him through the point, and a gait allows v(g) = 2R tan(ω / 2) per update, ω its
/// AI turn per update (with the turn boost). `speed` itself when its own gait allows it (uncapped by that gait's
/// speed); otherwise the best allowed, stepping down the gaits while each allows at least as much (capped by its own
/// speed), never above `speed`. 0 only when R is 0 (the point dead ahead, behind or on him) or `speed` is not above 0.
/// @orig 0x002fcd90 Move_CornerSpeedLimit (unknown)
[[nodiscard]] float cornerSpeedLimit(const Brain& brain, anim::Vec3 point, float speed);
/// Whether the first ground below `point` (a cast from kGroundProbeLift above it, kGroundProbeLength long, mask 0) is
/// a triangle with kGroundProbeFlag. No ground, or any other first, is false.
/// @orig 0x00249050 Ground_ProbeBelow (unknown)
[[nodiscard]] bool groundProbeBelow(const raycast::CollisionMesh& mesh, anim::Vec3 point);
/// Takes a detour at `point` round `blocker` when the human can walk straight there (the planner's walkable line; no
/// planner, it can) and the ground probe there is false (with no collision mesh, it is): avoiding him, the point held,
/// the score `fraction`. The decisions since a detour go on counting. Returns whether it did.
/// @orig 0x00289010 Steering_TryDetour (unknown)
bool tryDetour(Brain& brain, anim::Vec3 point, Brain& blocker, float fraction);

/// What a move hands its steering each update.
struct SteerRequest {
    float moveSpeed = 0.0F;     ///< The move's speed this update (its gait's, or the override).
    anim::Vec3 aim;             ///< Its aim: the route's waypoint, or its point.
    anim::Vec3 destination;     ///< Its point.
    float arrivalRadius = 0.0F; ///< Its arrival radius.
};

/// One update of the steering round humans for a move: nothing when the move keeps its own aim, else the aim to take
/// (within kSteerAimRadius), unchanged when only the speed override was set. Counts the override down first.
/// **Coney choices**: the human's detail level (`+0x333`) is always 0, so a decision is made every update; nobody is
/// held in a waypoint queue; a tie of speeds goes to the lower brain slot (an address would not repeat); both brains
/// following a route stands for the route state's `+0x04` and `+0x16`; a step longer than its way less the arrival
/// radius is cut to 0, never turned back; at the walker's own position there is nothing to steer.
/// @orig 0x00289138 Human_SteerAroundHumans (unknown)
[[nodiscard]] std::optional<anim::Vec3> steerAroundHumans(Brain& brain, const SteerRequest& request);

/// The step from `from` toward `to` in plan, of length min(the distance, `maxLength`) (z 0).
/// @orig 0x00288f40 Steering_ClampStep (unknown)
[[nodiscard]] anim::Vec3 clampStep(anim::Vec3 from, anim::Vec3 to, float maxLength);

/// Where along a ray from `origin` along `ray` (its length is the ray's) it first meets the kBlockerRadius disc round
/// `centre`, measured to the disc's edge; nothing when the centre is farther than the ray's length plus the radius,
/// behind the ray, or the ray passes outside the disc. **Coney choice**: a ray starting inside the disc hits at 0,
/// and a ray of length 0 never hits.
/// @orig 0x00289d68 Steering_RayHitsHuman (unknown)
[[nodiscard]] std::optional<float> rayHitsHuman(anim::Vec3 origin, anim::Vec3 ray, anim::Vec3 centre);

/// The step a human is expected to take next (in plan): a player's facing × kStepSeconds × his speed; an AI's toward
/// his brain's aim point, of length min(the distance to it, kStepSeconds × his brain's move speed).
[[nodiscard]] anim::Vec3 predictedStep(const Brain& brain);

/// The humans within `radius` (in plan) of `owner`'s human, in any direction, the owner and those out of the world
/// left out, at most kSteerListMax of them in the order given (`Humans_FindAhead`).
/// @orig 0x002274a8 Humans_FindAhead (unknown)
[[nodiscard]] std::vector<Brain*> humansAround(const Brain& owner, std::span<Brain* const> others, float radius);

/// The first human in the way of a step.
struct Blocker {
    Brain* brain = nullptr; ///< Who.
    float fraction = 0.0F;  ///< How far along the relative step the walker meets his disc (0-1).
};

/// The blocker test: of `list`, leaving out `walker`'s target and anyone behind him (the offset to him against
/// `direction`, the walker's unit move direction, below 0), the human whose disc the walker's `step` taken relative
/// to his predicted step meets first, as a fraction of that relative step; nothing when no disc is met.
/// **Coney choice**: a relative step of length 0 meets nobody.
/// @orig 0x00288cc8 Steering_FindBlocker (unknown)
[[nodiscard]] std::optional<Blocker> findBlocker(const Brain& walker, std::span<Brain* const> list, anim::Vec3 position,
                                                 anim::Vec3 direction, anim::Vec3 step);

/// The sectors giving way tries, as offsets from the one straight off the mover's path, in order (*s* − 3 is never
/// tried): straight off, then leaning anticlockwise.
inline constexpr std::array<int, 7> kGiveWayOrder{0, 1, 2, -1, 3, 4, -2};

/// The stander's sector pointing straight away from a mover's path: of the direction to him from the point closest
/// to him on the mover's line (`moverPosition` along `moverStep`). **Coney choice**: the line is taken unbounded
/// (the original's `Line_ClosestPoint` `0x00336e08` is not traced), and a step of length 0 is the mover's position.
[[nodiscard]] int giveWayStart(const human::Human& stander, anim::Vec3 moverPosition, anim::Vec3 moverStep);

/// The sector a standing AI steps into to give way to a mover: the first of kGiveWayOrder round giveWayStart() that
/// is free in his sector record (at most kSectorGiveWayAgeMs old); nothing when none is.
/// @orig 0x00289ed0 Brain_GiveWayTo (unknown)
[[nodiscard]] std::optional<int> giveWaySector(Brain& stander, anim::Vec3 moverPosition, anim::Vec3 moverStep);

} // namespace coney::ai
