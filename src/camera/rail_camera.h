// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "animation/anim_math.h"
#include "camera/camera_view.h"

// The rail camera (`Cam_Rail`, type 9, `CamSetupRail`): a camera that slides along a line of points the scripts lay
// (`CamAddRailPoint`) while it frames its target, the chases' camera. In mode 0 it stands at the target point's foot on
// the rail, within a reach of the target and below a ceiling; `CamLeadRail` puts it a lead ahead of or behind the
// target along the rail (modes 1 and 2), and `CamModifyRail` eases its settings over time. One per player; Coney has
// player 1's. Research: docs/research/camera.md#rail, docs/references/bindings/camera.md#camsetuprail

namespace coney::camera {

/// `CamSetupRail(name, target, fov, offset, near, far)`'s numbers.
struct RailSetup {
    float fieldOfView = 60.0F;
    anim::Vec3 offset;      ///< The look-at offset from the target, metres (`+0x320`); its z starts setting 9.
    float nearClip = 0.1F;  ///< Metres.
    float farClip = 150.0F; ///< Metres, at most RailCamera::kMaxFarClip.
};

/// How the rail camera places itself (`+0x3ed`).
enum class RailMode : std::uint8_t {
    Level,    ///< Mode 0, after `CamSetupRail`: at the target point's foot on the rail.
    Ahead,    ///< Mode 1: `lead` metres ahead of the target along the rail.
    Behind,   ///< Mode 2: `lead` metres behind it.
    Trailing, ///< Mode 3 (`CamModifyRail(5, pitch)`, the Armies levels): trails the target forward only, at a pitch.
};

/// One of the rail camera's settings, eased linearly to its target over the time left.
struct EasedValue {
    float current = 0.0F;
    float target = 0.0F;
    float secondsLeft = 0.0F;

    /// Sets a new target reached in `seconds` (at once for 0 or less).
    /// @coney-only Coney's store of a setting's target and time; the original writes the two fields in place
    void set(float value, float seconds);
    /// One update of `seconds`: the current value moved by the remaining distance over the remaining time.
    /// @orig 0x0013f930 CamRail_EaseValue (unknown)
    void step(float seconds);
};

/// Player 1's rail camera.
class RailCamera {
  public:
    /// At most this many points; further ones overwrite the last.
    static constexpr std::size_t kMaxPoints = 16;
    /// The far clip is at most this.
    static constexpr float kMaxFarClip = 150.0F;

    /// `CamModifyRail`'s settings Coney applies (docs/research/camera.md#rail).
    static constexpr std::uint32_t kDistance = 0; ///< The most the camera stays from its target point in plan, 0 off.
    static constexpr std::uint32_t kHeight = 1;   ///< The most it stands above the target's feet; negative off.
    static constexpr std::uint32_t kFieldOfView = 2; ///< Degrees; negative ignored.
    static constexpr std::uint32_t kShift = 3;       ///< The target point's shift along the rail, metres.
    static constexpr std::uint32_t kLookShift = 4;   ///< The look-at point's further shift along the rail, metres.
    /// Degrees; at or above -360 switches the rail to mode 3 (RailMode::Trailing) and is its view pitch, below it
    /// goes back to mode 0.
    static constexpr std::uint32_t kMode3Pitch = 5;
    /// Metres: mode 3's margin inside the view's sides for the targets (eased, kept for the keep-in-frame step).
    static constexpr std::uint32_t kMode3Margin = 7;
    static constexpr std::uint32_t kPitch = 8;      ///< A fixed look-at pitch, degrees; below -360 off.
    static constexpr std::uint32_t kLookHeight = 9; ///< The look-at offset's height, metres.
    /// An angle below this is off.
    static constexpr float kAngleOff = -360.0F;

    /// `CamSetupRail`'s reset: the set-up kept, no points, mode Level, the distance and shifts 0, the height and the
    /// pitch off, the field of view's target the new one; the lead is kept, as the original's reset leaves it.
    /// @orig 0x0011cce8 Camera_SetupRail (unknown)
    /// @orig 0x0013b2b8 CamRail_Reset (unknown)
    void setup(const RailSetup& setup);

    /// `CamAddRailPoint`: a point appended to the rail (the 16th overwritten past kMaxPoints).
    /// @orig 0x0013b780 CamRail_AppendPoint (unknown)
    void addPoint(anim::Vec3 point);
    /// How many points the rail has.
    [[nodiscard]] std::size_t pointCount() const { return m_count; }
    /// The rail point `i` (below pointCount()).
    [[nodiscard]] anim::Vec3 point(std::size_t i) const { return m_points.at(i); }

    /// `CamLeadRail(lead, seconds, ahead)`: mode Ahead or Behind, the lead eased to `lead` metres over `seconds` (at
    /// once while the current lead is negative).
    /// @orig 0x0011cf70 Camera_SetRailLead (unknown)
    void setLead(float lead, float seconds, bool ahead);
    /// `CamModifyRail(param, value, seconds)`: one setting eased to `value`. The distance and the height switched on
    /// from off start from the camera's present ones, and a height set negative eases back to the present height
    /// before it switches off; the pitch switched on starts from the present one. Setting 5 switches mode 3 on or off
    /// (docs/research/camera.md#rail-mode3). Setting 6 is eased by the original and never read, so it is not kept.
    /// @orig 0x0011d228 Camera_ModifyRail (unknown)
    /// @orig 0x0013fac8 CamRail_SetMode3Angle (unknown)
    void modify(std::uint32_t param, float value, float seconds);

    /// `CameraMakeActive` on the rail: the field of view and every eased setting but the look-at height and mode 3's
    /// margin at their targets at once, the segment hand-over cleared, and mode 3 starting over on the next update
    /// (`+0x3e7`), which does not damp the points (`+0x3e8`). Cameras::makeActive then runs that update, of
    /// kActivationSeconds, when the rail has two points or more (`CamRail_Reset`).
    /// @orig 0x0013b508 CamRail_Activate (unknown)
    void activate();
    /// The update an activation runs, seconds (`0x3c888889`).
    static constexpr float kActivationSeconds = 1.0F / 60.0F;

    /// One update of `seconds` with the target's feet at `feet` (nothing: no target, the update stops after the
    /// easing) and his locomotion's velocity `velocity` (mode 3 reads it): the settings eased, the target and look-at
    /// points made and damped, then the camera placed by its mode; with `leadLook` (switch 7) a leading mode moves the
    /// look-at point along the rail by the lead. That reading of switch 7 is a stand-in (#1017): the original's puts
    /// the camera off the rail beside the target and looks at the target point.

    /// @orig 0x0013d010 CamRail_Update (unknown)
    void update(std::optional<anim::Vec3> feet, bool leadLook, float seconds, anim::Vec3 velocity = {});

    /// What it shows: the view the update's end builds from the position and look-at point, in every mode.
    /// @orig 0x0013d010 CamRail_Update (unknown)
    /// @orig 0x0013dcc8 CamRail_PlaceMode3 (unknown)
    [[nodiscard]] CameraView view() const;
    /// How it places itself.
    [[nodiscard]] RailMode mode() const { return m_mode; }
    /// The lead along the rail now, metres.
    [[nodiscard]] float lead() const { return m_lead.current; }
    /// Mode 3's pitch now (setting 5), degrees; below kAngleOff while off.
    /// @coney-only a query for the tests; the original reads the setting in place
    [[nodiscard]] float mode3Pitch() const;
    /// The field of view now, degrees.
    [[nodiscard]] float fieldOfView() const { return m_fieldOfView.current; }
    /// Where it is.
    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// The point it looks at.
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// The target point of the last update (`P`).
    [[nodiscard]] anim::Vec3 targetPoint() const { return m_target; }
    /// The rail segment it is on (its first point's index).
    [[nodiscard]] std::size_t segment() const { return m_segment; }
    /// Whether it is held at an end of the rail (`+0x3e4`).
    [[nodiscard]] bool held() const { return m_held; }

  private:
    // update()'s body: the easing and the placement by mode.
    /// @orig 0x0013d010 CamRail_Update (unknown)
    void placeFor(std::optional<anim::Vec3> feet, bool leadLook, float seconds, anim::Vec3 velocity);
    // The target and look-at points of mode 0 or the leading modes from the feet.
    /// @orig 0x0013bad0 CamRail_UpdateTargetPoint (unknown)
    void makePoints(anim::Vec3 feet);
    // Damps the target and look-at points toward last update's.
    /// @orig 0x0013bfe0 CamRail_DampTargetPoint (unknown)
    void damp(anim::Vec3 oldTarget, anim::Vec3 oldLook);
    // Mode 0's placement; the projection on the segment and setting 0's pull are CamRail_ProjectOnSegment's, inlined.
    /// @orig 0x0013d6b0 CamRail_UpdatePosition (unknown)
    /// @orig 0x00140308 CamRail_ProjectOnSegment (unknown)
    void placeLevel(anim::Vec3 feet);
    // Modes 1 and 2: the lead ahead of or behind the target's place on the rail.
    /// @orig 0x0013e708 CamRail_PlaceLeading (unknown)
    void placeLeading(anim::Vec3 feet, bool leadLook);
    // Mode 3's placement (CamRail_PlaceMode3): the start after an activation, else the trailing move.
    /// @orig 0x0013dcc8 CamRail_PlaceMode3 (unknown)
    void placeTrailing(std::optional<anim::Vec3> feet, anim::Vec3 velocity, float seconds);
    // Mode 3's look-at point: 2 m from `from` toward `toward` in plan, at the mode's pitch.
    /// @orig 0x0013dcc8 CamRail_PlaceMode3 (unknown)
    [[nodiscard]] anim::Vec3 pitchedLook(anim::Vec3 from, anim::Vec3 toward) const;
    // Mode 3's projection of `p` on the current segment, in plan, clamped only at the rail's two ends.
    /// @orig 0x0013dcc8 CamRail_PlaceMode3 (unknown)
    [[nodiscard]] anim::Vec3 projectTrailing(anim::Vec3 p) const;
    // Picks the segment the target point is on (CamRail_ChooseSegment).
    void chooseSegment() { chooseSegmentFor(m_target); }
    // Picks the segment point `p` is on; a change sets the hand-over blend.
    /// @orig 0x0013fba0 CamRail_ChooseSegment (unknown)
    void chooseSegmentFor(anim::Vec3 p);
    // The current segment's direction flattened to the ground plane and normalised; +y with no segment: the
    // direction step of the target point's shifts.
    /// @orig 0x0013bad0 CamRail_UpdateTargetPoint (unknown)
    [[nodiscard]] anim::Vec3 flatDirection() const;
    // Where `p` projects on segment `i` (0 at its first point, its length at the second) and the segment's length:
    // the projection step of CamRail_ProjectOnSegment.
    /// @orig 0x00140308 CamRail_ProjectOnSegment (unknown)
    [[nodiscard]] std::pair<float, float> project(std::size_t i, anim::Vec3 p) const;
    // The present pitch from the camera to its look-at point, degrees.
    /// @orig 0x0013f9b0 CamRail_PitchToPoint (unknown)
    [[nodiscard]] float presentPitch() const;

    RailSetup m_setup;
    std::array<anim::Vec3, kMaxPoints> m_points{};
    std::size_t m_count = 0;
    std::size_t m_segment = 0;
    RailMode m_mode = RailMode::Level;
    EasedValue m_lead;
    EasedValue m_distance;
    EasedValue m_height{.current = -1.0F, .target = -1.0F, .secondsLeft = 0.0F};
    EasedValue m_fieldOfView;
    EasedValue m_shift;
    EasedValue m_lookShift;
    EasedValue m_pitch{.current = -std::numeric_limits<float>::max(),
                       .target = -std::numeric_limits<float>::max(),
                       .secondsLeft = 0.0F};
    EasedValue m_lookHeight;
    // Setting 5, radians; off (lowest float) outside mode 3.
    EasedValue m_mode3Pitch{.current = -std::numeric_limits<float>::max(),
                            .target = -std::numeric_limits<float>::max(),
                            .secondsLeft = 0.0F};
    EasedValue m_mode3Margin;        // setting 7
    bool m_starting = false;         // +0x3e7: mode 3 starts over on the next update
    bool m_undamped = false;         // +0x3e8: the next update does not damp the points
    anim::Vec3 m_goal;               // +0x310: mode 3's goal for the look-at point
    bool m_heightOffPending = false; // a negative height eases back to the present height, then switches off
    float m_blend = 0.0F;            // +0x3cc: the soft hand-over between segments, 0 when none
    bool m_held = false;
    anim::Vec3 m_target;   // P
    anim::Vec3 m_position; // the camera
    anim::Vec3 m_lookAt;
    anim::Vec3 m_feet; // the target's feet at the last update
    bool m_placed = false;
};

} // namespace coney::camera
