// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include "animation/anim_math.h"

// The aiming state of a throw (L1 with a bottle, a brick or a ball in hand): the aim's origin, heading and pitch, the
// stick that turns and pitches it, the arc traced from the hand each frame and the human or object the arc meets.
// Research: docs/research/objects.md#throws

namespace coney::combat {

/// The pitch the aim starts at and goes back to after a throw (`+0x630`), radians (9°).
inline constexpr float kAimStartPitch = 0.157F;
/// The pitch's limit either way, radians (36°).
inline constexpr float kAimPitchLimit = 0.628F;
/// What one step of the stick's value turns or pitches the aim by in a frame, radians.
inline constexpr float kAimRate = 0.000589F;
/// The stick's dead zone (an offset from the centre, of 128), and what is taken off an offset outside it.
inline constexpr int kAimDeadZone = 26;
inline constexpr int kAimStickLess = 25;
/// The release point in the thrower's frame (x right, y forward, z up), metres from the aim's origin.
inline constexpr std::array<float, 3> kAimReleaseOffset{0.3009F, -0.6354F, 1.5865F};
/// The arc's steps of 1/60 s, the z speed each takes away (15.68 m/s² over a step), and every how many steps a point
/// is kept (18 points, 17 segments).
inline constexpr int kAimTraceSteps = 108;
inline constexpr float kAimTraceGravityStep = 0.2613F;
inline constexpr int kAimTraceKeep = 6;
inline constexpr int kAimTracePoints = kAimTraceSteps / kAimTraceKeep;
/// The frames the thrower turns straight to a newly found human target.
inline constexpr int kAimFaceFrames = 5;

/// What one sweep of the aim's sphere along a segment met: the task's handle (0 for the level mesh) and how far along
/// the segment, 0 to 1.
struct AimHit {
    double handle = 0;
    float fraction = 0.0F;
};

/// The world the arc is traced through. Each sweeps a sphere of `radius` from `from` to `to` and gives the first
/// contact.
struct AimWorld {
    /// Pass 1: the humans a throw may target (an AI human with body flag `THROWNWEAPONTARGET`, not friendly to the
    /// thrower, not the thrower; with the stick pushed sideways, `side` −1 left or 1 right, only one on that side of
    /// him; 0 any). `ThrowArc_TargetFilter`.
    std::function<std::optional<AimHit>(anim::Vec3 from, anim::Vec3 to, float radius, int side)> humans;
    /// Pass 2: anything in the way: the level mesh (handle 0) or any body with `THROWNWEAPONTARGET` and none of
    /// `0x180000`.
    std::function<std::optional<AimHit>(anim::Vec3 from, anim::Vec3 to, float radius)> anything;
    /// Where a human target is aimed at; nothing when it is gone.
    std::function<std::optional<anim::Vec3>(double human)> targetPoint;
};

/// One frame's input to the aim.
struct AimInput {
    /// The left stick's raw offsets from the centre (bytes − 128): x right, y down.
    int stickX = 0;
    int stickY = 0;
    /// On the aim's first frame, the follow camera's horizontal look heading when it is the active camera (type 2,
    /// one view per player); nothing under any other camera.
    std::optional<float> followHeading;
    /// The held object's weight factor (combat::throwWeightFactor()) and the sweep's radius (half the type's `+0x78`).
    float weightFactor = 1.0F;
    float radius = 0.0F;
};

/// A player's aiming state (`Human_EnterThrowAim`, `Human_MoveThrowAim`, `Human_TraceThrowAim`): entered with L1 and a
/// set 5 object in hand, left with L1 again, a throw, or the object lost. **Coney's readings**: the stick pushed up
/// raises the pitch; the side filter reads "sideways" as the stick's x outside the dead zone.
class ThrowAimState {
  public:
    /// Enters the aim for a human standing at `origin` facing `heading` (radians, 0 facing +y): the pitch at
    /// kAimStartPitch, the first frame still to come.
    /// @orig 0x00227b30 Human_EnterThrowAim (unknown)
    /// @orig 0x002446b0 Human_SetThrowAimState (unknown)
    void enter(anim::Vec3 origin, float heading);
    /// Leaves the aim (L1 again, a throw, the object lost); the pitch goes back to kAimStartPitch. The last object
    /// aimed at is kept, as `+0x634` is.
    /// @orig 0x00227a90 Human_ExitThrowAim (unknown)
    void leave();
    [[nodiscard]] bool active() const { return m_active; }

    /// One aiming frame: on the first, the follow camera's heading; then the trace; then a turn straight to a human
    /// target (the kAimFaceFrames frames after a new one, and while the stick is centred), or else the stick's turn and
    /// pitch. Returns whether the stick turned him this frame (his turn clip plays).
    /// @orig 0x00244770 Human_MoveThrowAim (unknown)
    bool step(const AimInput& input, const AimWorld& world);

    /// The heading the thrower faces, radians.
    [[nodiscard]] float heading() const { return m_heading; }
    /// The aim's pitch (`+0x630`), radians.
    [[nodiscard]] float pitch() const { return m_pitch; }
    /// The release point (`+0x5f0`) and the throw's velocity in the world (`+0x600`), from the last trace.
    [[nodiscard]] anim::Vec3 releasePoint() const { return m_release; }
    [[nodiscard]] anim::Vec3 velocity() const { return m_velocity; }
    /// The human target the last trace found (`+0x638`); 0 for none.
    [[nodiscard]] double target() const { return m_target; }
    /// What the last trace met (`+0x634`): the human target or the first object with `THROWNWEAPONTARGET`; 0 for a
    /// wall, the ground or nothing. Kept after the aim ends (HuIsAimingAt).
    [[nodiscard]] double aimed() const { return m_aimed; }
    /// The arc's kept points up to where it ended, for the arc's drawing.
    [[nodiscard]] const std::array<anim::Vec3, kAimTracePoints>& arc() const { return m_arc; }
    [[nodiscard]] int arcPoints() const { return m_arcPoints; }

    /// The trace alone (`Human_TraceThrowAim`): the release point and velocity from the origin, heading and pitch, the
    /// arc's 108 steps and its two sweeps. Public for the tests.
    /// @orig 0x0018fee0 Human_TraceThrowAim (unknown)
    void trace(const AimInput& input, const AimWorld& world, int side);

  private:
    bool m_active = false;
    bool m_firstFrame = false; // +0x256 still clear
    anim::Vec3 m_origin{};     // +0x610
    float m_heading = 0.0F;    // +0x620
    float m_pitch = kAimStartPitch;
    anim::Vec3 m_release{};
    anim::Vec3 m_velocity{};
    double m_target = 0;  // +0x638
    double m_aimed = 0;   // +0x634
    int m_faceFrames = 0; // +0x63c
    std::array<anim::Vec3, kAimTracePoints> m_arc{};
    int m_arcPoints = 0;
};

/// A point given in a thrower's frame (x right, y forward, z up) turned into the world by his `heading`.
[[nodiscard]] anim::Vec3 throwerToWorld(anim::Vec3 local, float heading);

} // namespace coney::combat
