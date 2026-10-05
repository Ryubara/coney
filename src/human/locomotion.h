// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <numbers>
#include <span>

#include "animation/anim_math.h"

// The player's locomotion as plain functions: the stick turned into an intent, the target speed, the gait, the turn
// with its limit and ease, the acceleration, the slope factor and the gait blend's value for a speed. No state but
// what the caller passes, so each rule can be tested on its own. Every per-update constant is the original's at its
// 30 Hz step (docs/research/characters.md#movement-constants).
// Research: docs/research/characters.md#locomotion

namespace coney::human {

/// The characters' step: the original's humans update 30 times a second, and every per-update limit below assumes it.
inline constexpr float kStepSeconds = 1.0F / 30.0F;
/// Stick magnitude at or below which the stick does nothing.
inline constexpr float kStickDeadZone = 0.12F;
/// Stick magnitude above which a run is allowed (0x005102e8).
inline constexpr float kRunThreshold = 0.95F;
/// Speed gained per second while speeding up (0.8 m/s per update).
inline constexpr float kAcceleration = 24.0F;
/// Below this speed the gait is 0 (standing).
inline constexpr float kStandingSpeed = 0.5F;
/// The ease's full-rate heading error (0x00510310), and the share of the last turn step carried over (0x0051030c), as
/// `config_preload2.lua`'s `CfgTurnRate(true, false, 2.0, 0.8)` sets them in play (`.data` holds 1.5 rad for the ease;
/// docs/research/characters.md#movement-constants).
inline constexpr float kTurnEaseError = 2.0F;
inline constexpr float kTurnCarry = 0.8F;
/// The player's turn limits per update in play, degrees: the player words of the table at 0x005101b0 as
/// `config_preload2.lua`'s `CfgSetTurnRates(11, 16, 18, 18, 20, 24)` sets them (the `.data` values 12°, 6°, 4° and
/// 2.5° are the other humans' column; docs/research/feel.md#details-behind-the-table).
inline constexpr float kSprintTurnDegrees = 16.0F;
inline constexpr float kRunTurnDegrees = 18.0F;
inline constexpr float kJogTurnDegrees = 18.0F;
inline constexpr float kWalkTurnDegrees = 20.0F;
inline constexpr float kStanceTurnDegrees = 24.0F;
/// The carry when the heading error has changed sign since the last update.
inline constexpr float kTurnReverseCarry = -0.5F;
/// Skid: in a run or sprint, a stick under this magnitude, or pointing more than 120° from the velocity.
inline constexpr float kSkidStick = 0.2F;
inline constexpr float kSkidDot = -0.5F;

/// The locomotion values the debug menus may edit while the game runs (Coney's tunables,
/// docs/guides/debug-menu.md#tunables). Each defaults to the researched constant above; the game reads them through
/// locomotionTuning(), and the tunables registry changes them only between two steps, so a run stays deterministic.
struct LocomotionTuning {
    float stickDeadZone = kStickDeadZone;         ///< kStickDeadZone.
    float runThreshold = kRunThreshold;           ///< kRunThreshold.
    float acceleration = kAcceleration;           ///< kAcceleration, m/s each second.
    float walkTurnDegrees = kWalkTurnDegrees;     ///< maxTurn() walking, standing or sneaking, degrees an update.
    float jogTurnDegrees = kJogTurnDegrees;       ///< maxTurn() jogging.
    float runTurnDegrees = kRunTurnDegrees;       ///< maxTurn() running.
    float sprintTurnDegrees = kSprintTurnDegrees; ///< maxTurn() sprinting.
    float stanceTurnDegrees = kStanceTurnDegrees; ///< stanceTurn(): in a combat stance (the block's shuffle).
    float turnEaseError = kTurnEaseError;         ///< turnToward(): the error at which the ease is full, radians.
    float turnCarry = kTurnCarry;                 ///< turnToward(): the share of the last step carried over.
    float leanFactor = 0.4F;     ///< leanStep(): the turn times this times the speed is the lean asked for.
    float walkLeanFactor = 8.0F; ///< leanStep(): the same, walking.
};

/// The one LocomotionTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] LocomotionTuning& locomotionTuning();

/// Gaits, as the original numbers them (0x0022aeb0, 0x00221760).
enum class Gait : std::uint8_t { Standing = 0, Sneak = 1, Walk = 2, Jog = 3, Run = 4, Sprint = 5 };

/// A human's speeds in m/s, each from its locomotion clip's root motion (docs/research/characters.md#speed-classes).
struct Speeds {
    float base = 0.0F;   ///< Slot 14, the combat walk.
    float sneak = 0.0F;  ///< Slot 3.
    float walk = 0.0F;   ///< Slot 4.
    float jog = 0.0F;    ///< Slot 5.
    float run = 0.0F;    ///< Slot 6.
    float sprint = 0.0F; ///< Slot 7.
};

/// The left stick as the human sees it: turned into the camera's frame, as an angle and a length.
struct StickIntent {
    float angle = std::numbers::pi_v<float> / 2.0F; ///< atan2 of the camera-turned stick, radians.
    float magnitude = 0.0F;                         ///< Its length, clamped to 1.
};

/// Turn smoothing carried from one update to the next (`+0x5e0`, `+0x5e4`).
struct TurnState {
    float lastStep = 0.0F;  ///< The last update's turn step, radians (never negative).
    float lastError = 0.0F; ///< The last update's heading error, radians.
};

/// The angle wrapped to (-π, π].
[[nodiscard]] float wrapAngle(float radians);

/// The unit direction a heading faces in the game's axes: heading 0 faces +y, and the heading grows anticlockwise
/// seen from above (towards -x). The same convention the clips use (a walk's root velocity points along +y).
[[nodiscard]] anim::Vec3 facing(float heading);

/// The heading a direction in the xy plane faces (the inverse of facing()).
[[nodiscard]] float headingOf(anim::Vec3 direction);

/// The stick (x right, y up, each -1 to 1) turned into the frame of a camera whose horizontal view direction is
/// `cameraForward` (need not be unit length; with no horizontal part the stick is taken as is), so that up moves away
/// from the camera; its angle and its length clamped to 1. With `locked` (`+0x1f`) the angle is π/2 and the length 0.
/// @orig 0x00146078 PlayerRecord_Update (unknown)
[[nodiscard]] StickIntent stickIntent(float stickX, float stickY, anim::Vec3 cameraForward, bool locked = false);

/// The speed the stick asks for: 0 inside the dead zone, otherwise the walk speed (how far the stick is pushed does not
/// matter beyond that); above kRunThreshold the run speed, or the sprint speed when `sprinting` (state flag
/// `0x1000000` set and stamina not 0, `0x00225dc0`). Jog and the state overrides are not modelled.
[[nodiscard]] float targetSpeed(float magnitude, const Speeds& speeds, bool sprinting = false);

/// The gait stored with a velocity (`+0x1a8`): standing below 0.5 m/s, otherwise the gait 1-5 whose speed is nearest.
/// @orig 0x0022aeb0 Human_GaitOfVelocity (unknown)
[[nodiscard]] Gait gaitOfSpeed(float speed, const Speeds& speeds);

/// The gait a speed reaches: sprint at or above the sprint speed, run at or above the run speed, then jog, walk,
/// otherwise standing.
/// @orig 0x00221760 Human_GaitForSpeed (unknown)
[[nodiscard]] Gait gaitForSpeed(float speed, const Speeds& speeds);

/// The player's turn limit per update for a gait, radians: in play 20° walking (and standing or sneaking), 18° jogging
/// and running, 16° sprinting (locomotionTuning()).
/// @orig 0x002213d8 Human_MaxTurn (unknown)
[[nodiscard]] float maxTurn(Gait gait);

/// The player's turn limit per update in a combat stance (state `0x00228340`), radians: 24° in play.
[[nodiscard]] float stanceTurn();

/// What `CfgSetTurnRates` does for the player: the six player turn limits of the table at 0x005101b0, in degrees, in
/// the script's order (two special states', sprint, run, jog, walk, combat stance). A value outside 0 to 90 leaves the
/// old one, as the original does; the first (two special states Coney does not model) is dropped, and missing values
/// leave theirs. Writes locomotionTuning().
/// @orig 0x0023a7a0 Cfg_SetTurnRates (unknown)
void setPlayerTurnRates(std::span<const float> degrees);

/// What `CfgTurnRate(smoothed, other, easeError, carry)` does for the turn: the ease's full-rate error (radians) and
/// the carried share of the last step. Coney's turn is always eased, so the two flags are not taken. Writes
/// locomotionTuning().
/// @orig 0x0023a5f8 Cfg_SetTurnRate (unknown)
void setTurnEase(float easeError, float carry);

/// Turns `heading` toward `target` by at most `limit` radians, eased: the step is the limit times
/// `(1 - cos(π e / E)) / 2` (e the error, capped at E, 2.0 rad in play) plus 0.8 times the last step (-0.5 times it
/// when the error changed sign), clamped to [0, limit]; when the error is smaller than the step the heading snaps to
/// the target. Updates `state`. Returns the new heading, wrapped.
[[nodiscard]] float turnToward(float heading, float target, float limit, TurnState& state);

/// The next speed toward `target`: up by kAcceleration × `seconds` at most, down to the target at once.
[[nodiscard]] float approachSpeed(float current, float target, float seconds);

/// Whether a running human skids to a stop: in gait 4 or 5, at the run speed or above, after a stick that was over
/// kRunThreshold, the stick now under 0.2 or more than 120° from the velocity.
[[nodiscard]] bool skids(Gait gait, float speed, const Speeds& speeds, float lastMagnitude, float magnitude,
                         anim::Vec3 velocityDirection, anim::Vec3 stickDirection);

/// The factor a slope scales a grounded human's velocity by: 1 on ground whose normal's z is 0.95 or more,
/// otherwise clamp(0.6 + 0.3 (n.z - 0.5), 0.5, 1).
[[nodiscard]] float slopeFactor(float normalZ);

/// The body's lean one update on (`+0x29c`, radians, positive leaning into a turn to the left): the turn this update
/// (`turn`, radians, positive anticlockwise) times a factor (8 × `speed` walking, 0.4 × `speed` otherwise) is the
/// target (`+0x298`), clamped to 2°, 3°, 5° or 7°; the lean moves 0.625 of the way toward it, at most 1°, 1.2°, 1.3°
/// or 1.8° an update. **Coney's reading**: the four limits go with walking (and standing), jogging, running and
/// sprinting in that order; the page says only "by gait".
/// @orig 0x00248df0 Human_Lean (unknown)
[[nodiscard]] float leanStep(float lean, float turn, float speed, Gait gait);

/// The gait blend's target for a speed (0 walk, 1 jog, 2 run, 3 sprint, linear between, clamped to 0-3).
/// @orig 0x0025ec28 Gait_BlendForSpeed (unknown)
[[nodiscard]] float gaitBlendForSpeed(float speed, const Speeds& speeds);

} // namespace coney::human
