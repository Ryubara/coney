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
    Level,  ///< Mode 0, after `CamSetupRail`: at the target point's foot on the rail.
    Ahead,  ///< Mode 1: `lead` metres ahead of the target along the rail.
    Behind, ///< Mode 2: `lead` metres behind it.
};

/// One of the rail camera's settings, eased linearly to its target over the time left.
struct EasedValue {
    float current = 0.0F;
    float target = 0.0F;
    float secondsLeft = 0.0F;

    /// Sets a new target reached in `seconds` (at once for 0 or less).
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
    static constexpr std::uint32_t kPitch = 8;       ///< A fixed look-at pitch, degrees; below -360 off.
    static constexpr std::uint32_t kLookHeight = 9;  ///< The look-at offset's height, metres.
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
    /// before it switches off; the pitch switched on starts from the present one. **Coney stand-in**: settings 5, 6
    /// and 7 (mode 3) are not traced and do nothing.
    /// @orig 0x0011d228 Camera_ModifyRail (unknown)
    void modify(std::uint32_t param, float value, float seconds);

    /// One update of `seconds` with the target's feet at `feet` (nothing: no target, the update stops after the
    /// easing): the settings eased, the target and look-at points made and damped, then the camera placed by its
    /// mode; with `leadLook` (switch 7) a leading mode moves the look-at point along the rail by the lead.
    /// @orig 0x0013d010 CamRail_Update (unknown)
    void update(std::optional<anim::Vec3> feet, bool leadLook, float seconds);

    /// What it shows.
    [[nodiscard]] CameraView view() const;
    /// How it places itself.
    [[nodiscard]] RailMode mode() const { return m_mode; }
    /// The lead along the rail now, metres.
    [[nodiscard]] float lead() const { return m_lead.current; }
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
    // The target and look-at points of mode 0 or the leading modes from the feet (CamRail_UpdateTargetPoint).
    void makePoints(anim::Vec3 feet);
    // Damps the target and look-at points toward last update's (0x0013bfe0).
    void damp(anim::Vec3 oldTarget, anim::Vec3 oldLook);
    // Mode 0's placement (CamRail_UpdatePosition).
    void placeLevel(anim::Vec3 feet);
    // Modes 1 and 2: the lead ahead of or behind the target's place on the rail (CamRail_PlaceLeading).
    void placeLeading(anim::Vec3 feet, bool leadLook);
    // Picks the segment the target point is on (CamRail_ChooseSegment).
    void chooseSegment();
    // The current segment's direction flattened to the ground plane and normalised; +y with no segment.
    [[nodiscard]] anim::Vec3 flatDirection() const;
    // Where `p` projects on segment `i` (0 at its first point, its length at the second) and the segment's length.
    [[nodiscard]] std::pair<float, float> project(std::size_t i, anim::Vec3 p) const;
    // The present pitch from the camera to its look-at point, degrees.
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
