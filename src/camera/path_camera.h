// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "animation/anim_math.h"
#include "camera/camera_view.h"

// The path camera (`Cam_Spline`, type 3, the scripts' "Poizo" camera): a scripted flight from one camera's view through
// up to seven more points along a Catmull-Rom curve, turning by slerp between the points' orientations, calling a
// script function at each point it reaches and at the end. There is one; `CamSetupPoizo` starts it over.
// Research: docs/research/camera.md#path-cameras, docs/references/bindings/camera.md#camsetuppoizo

namespace coney::camera {

/// One point of the path: where, which way, the seconds to the next point and the function called on reaching it.
struct PathPoint {
    anim::Vec3 position;
    anim::Quat orientation;
    float seconds = 0.0F; ///< To the next point; unused on the last.
    std::string onReach;  ///< Empty for none.
};

/// The path camera.
class PathCamera {
  public:
    /// The path holds at most this many points, the start view among them (`PoizoCam_AddPoint`).
    static constexpr std::size_t kMaxPoints = 8;

    /// `CamSetupPoizo`: the path starts over from `start` (point 0, its lens the path's unless `fieldOfView` or
    /// `farClip` is above 0, the far clip at most 150), `seconds` to the first point added, `onEnd` called at the last.
    /// It does not move until made active.
    /// @orig 0x0011c9e0 Camera_SetupPoizo (unknown)
    void setup(const CameraView& start, float seconds, std::string onEnd, float fieldOfView, float farClip);
    /// `CamAddPoizoPoint` / `CamAddPoizoPointCam`: a point appended; with the path full it replaces the last. A time
    /// below 0 is kept as 0.
    /// @orig 0x00142a58 PoizoCam_AddPoint (unknown)
    void addPoint(const PathPoint& point);
    /// Starts the flight from point 0 (the camera made current, `+0x33c`); nothing while it flies already.
    void activate();
    /// One update of `seconds` while it is the current camera: along the curve, each point reached adding its
    /// function to `fired`, and the end function once the last is reached, where it then stays.
    /// @orig 0x001426c0 PoizoCam_Update (unknown)
    void update(float seconds, std::vector<std::string>& fired);

    /// What it shows now.
    [[nodiscard]] CameraView view() const;
    /// The points, the start view first.
    [[nodiscard]] const std::vector<PathPoint>& points() const { return m_points; }
    /// Whether it reached its last point.
    [[nodiscard]] bool finished() const { return m_finished; }

  private:
    std::vector<PathPoint> m_points;
    std::string m_onEnd;
    float m_fieldOfView = 65.0F;
    float m_nearClip = 0.1F;
    float m_farClip = 115.0F;
    std::size_t m_segment = 0; // from point m_segment to the next
    float m_elapsed = 0.0F;    // seconds into the segment
    bool m_active = false;
    bool m_finished = false;
};

/// The orientation of a camera turned `headingDegrees` (0 facing +y, anticlockwise from above), pitched
/// `pitchDegrees` (positive looking down) and rolled `rollDegrees` (positive turning the top to the right), as a
/// locked camera reads its angles.
[[nodiscard]] anim::Quat orientationOf(float headingDegrees, float pitchDegrees, float rollDegrees);

} // namespace coney::camera
