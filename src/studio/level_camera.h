// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "animation/anim_math.h"
#include "studio/level_document.h"

// The Level view's fly camera and the view math its picking and markers share with the drawing, in game axes (metres,
// z up). The controls are the Level view design's (docs/guides/studio/level-view.md#fly-camera): right mouse held to
// look, W A S D along the view, Q and E to sink and rise (the speed grows the longer they are held), Shift faster, Alt
// slower, the wheel while the right button is held to change the base speed, which stays. The camera moves at a fixed
// step, never reading a clock, so a scripted input flies the same path every time. Coney's own; the original has no
// editor.

namespace coney::studio {

/// Where a camera is and where it looks, game axes: unit directions at right angles, `right` to the screen's right.
struct ViewPose {
    anim::Vec3 position;
    anim::Vec3 forward{0.0F, 1.0F, 0.0F};
    anim::Vec3 right{1.0F, 0.0F, 0.0F};
    anim::Vec3 up{0.0F, 0.0F, 1.0F};
};

/// A perspective view of a panel: the pose and the view window (half its width and height at distance 1).
struct LevelView {
    ViewPose pose;
    float halfWidth = 0.5F;
    float halfHeight = 0.5F;
    float nearClip = 0.1F;
    float width = 1.0F;  ///< The panel's size in pixels.
    float height = 1.0F; ///< The panel's size in pixels.
};

/// A point on the panel, pixels from its top left.
struct PanelPoint {
    float x = 0.0F;
    float y = 0.0F;
};

/// Where `point` (game axes) falls on the panel, or nothing when it is behind the near clip.
[[nodiscard]] std::optional<PanelPoint> project(const LevelView& view, anim::Vec3 point);

/// A ray from the camera through a panel point.
struct Ray {
    anim::Vec3 origin;
    anim::Vec3 direction; ///< Unit length.
};

/// The ray through the panel point `point`.
[[nodiscard]] Ray rayThrough(const LevelView& view, PanelPoint point);

/// The distance along `ray` to the triangle `a b c` (either side), or nothing when it misses.
[[nodiscard]] std::optional<float> hitTriangle(const Ray& ray, anim::Vec3 a, anim::Vec3 b, anim::Vec3 c);

/// The distance along `ray` to `box`, or nothing when it misses (0 when the ray starts inside).
[[nodiscard]] std::optional<float> hitBounds(const Ray& ray, const Bounds& box);

/// The entity a click at `point` selects: the nearest of the primitives' triangles and the point entities' boxes the
/// ray through it meets; nothing for empty space.
[[nodiscard]] std::optional<EntityId> pickEntity(const LevelDocument& level, const LevelView& view, PanelPoint point);

/// What the fly camera is given for one step.
struct FlyInput {
    bool flying = false;  ///< The right mouse button is held: look and the keys below move the camera.
    float lookX = 0.0F;   ///< Mouse movement this step, pixels, right positive.
    float lookY = 0.0F;   ///< Mouse movement this step, pixels, down positive.
    float forward = 0.0F; ///< W minus S, -1 to 1.
    float strafe = 0.0F;  ///< D minus A, -1 to 1.
    float rise = 0.0F;    ///< E minus Q, -1 to 1.
    bool fast = false;    ///< Shift held.
    bool slow = false;    ///< Alt held.
    float wheel = 0.0F;   ///< Wheel notches this step, away from the user positive.
    /// Radians the camera turns per pixel of lookX and lookY: FlyCamera::lookRadiansPerPixel() for the display and the
    /// user's sensitivity (docs/guides/studio/level-view.md#fly-look).
    float lookRadiansPerPixel = 0.0025F;
};

/// The Level view's fly camera.
class FlyCamera {
  public:
    /// The base speed a new camera flies at, metres a second: a jog, as the sandbox viewer's.
    static constexpr float kBaseSpeed = 6.0F;
    /// Shift's and Alt's factors.
    static constexpr float kFastFactor = 4.0F;
    static constexpr float kSlowFactor = 0.25F;
    /// Each wheel notch multiplies the base speed by this (or divides, towards the user).
    static constexpr float kWheelFactor = 1.25F;
    /// The base speed's range, metres a second.
    static constexpr float kMinSpeed = 0.5F;
    static constexpr float kMaxSpeed = 120.0F;
    /// How far it looks up or down, radians, short of straight up so the view never flips.
    static constexpr float kMaxPitch = 1.5F;

    /// A camera at `position` looking along the game's heading `yawDegrees` (0 along +y, counter-clockwise from above,
    /// as a layout's `view`) and `pitchDegrees` up.
    FlyCamera(anim::Vec3 position, float yawDegrees, float pitchDegrees);
    /// The camera at a layout's viewpoint.
    [[nodiscard]] static FlyCamera at(const sandbox::Viewpoint& view);

    /// The turn per pixel of mouse movement for a display `displayScale` times the 96 dpi desktop (the operating
    /// system's scale: 1, 1.5, 2) and a look `sensitivity` (1 is the default): sensitivity 1 turns the same for the
    /// same hand movement on any display, because pixels are counted in the display's scale-1 pixels
    /// (docs/guides/studio/level-view.md#fly-look). A scale of 0 or less counts as 1.
    [[nodiscard]] static float lookRadiansPerPixel(float displayScale, float sensitivity);
    /// The speed factor the camera's acceleration gives after flying `movingSeconds` without a pause: 1 at first,
    /// growing steadily to a ceiling (docs/guides/studio/level-view.md#fly-acceleration).
    [[nodiscard]] static float accelerationAfter(float movingSeconds);
    /// The factor now: accelerationAfter() of the time flown since the movement keys were last all up.
    [[nodiscard]] float acceleration() const { return accelerationAfter(m_movingSeconds); }

    /// How long a glide to frame the selection lasts, seconds (the maintainer's request: a quarter of a second).
    static constexpr float kGlideSeconds = 0.25F;

    /// One step of `seconds` with `input`: flying moves the camera (and ends a glide); otherwise a glide in progress
    /// goes on. Nothing moves without either.
    void step(const FlyInput& input, float seconds);
    /// Starts a glide that frames `box` (*F*, Unity's Frame Selected; docs/guides/studio/level-view.md#fly-camera): the
    /// camera keeps its heading and pitch and moves back along its view from the box's middle until the box's bounding
    /// sphere fits a view window whose narrower half is `halfHeight` (at least 1 m away). The glide eases in, slow at
    /// first and faster as it goes (the distance covered grows with the square of the time), and lasts kGlideSeconds.
    void frame(const Bounds& box, float halfHeight);
    /// Whether a glide is in progress.
    [[nodiscard]] bool gliding() const { return m_glideSeconds < kGlideSeconds; }
    /// Where the current or last glide ends.
    [[nodiscard]] anim::Vec3 glideTarget() const { return m_glideTo; }

    /// Where it is and where it looks.
    [[nodiscard]] ViewPose pose() const;
    /// Its position, game axes.
    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// Its heading, degrees: 0 along +y, counter-clockwise from above.
    [[nodiscard]] float yawDegrees() const;
    /// Its pitch, degrees, up positive.
    [[nodiscard]] float pitchDegrees() const;
    /// The base speed, metres a second, as the wheel left it.
    [[nodiscard]] float baseSpeed() const { return m_speed; }

  private:
    anim::Vec3 m_position;
    float m_yaw = 0.0F;   // radians, the game's heading: 0 along +y, counter-clockwise from above
    float m_pitch = 0.0F; // radians, up positive
    float m_speed = kBaseSpeed;
    float m_movingSeconds = 0.0F;         // flown without a pause: the acceleration's clock
    anim::Vec3 m_glideFrom;               // where the glide started
    anim::Vec3 m_glideTo;                 // where it ends
    float m_glideSeconds = kGlideSeconds; // time into the glide; kGlideSeconds or more: none
};

/// The view of `camera` for a panel of `width` × `height` pixels with the player camera's vertical view window
/// (`halfHeight`).
[[nodiscard]] LevelView levelView(const ViewPose& pose, float width, float height, float halfHeight, float nearClip);

} // namespace coney::studio
