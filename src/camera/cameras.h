// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "camera/camera_blend.h"
#include "camera/camera_lens.h"
#include "camera/camera_shake.h"
#include "camera/camera_view.h"
#include "camera/follow_camera.h"
#include "camera/locked_camera.h"
#include "camera/path_camera.h"
#include "camera/slow_motion.h"
#include "camera/win_camera.h"
#include "raycast/collision_mesh.h"

// Player 1's cameras, as the camera manager keeps them: the follow camera, the locked cameras the scripts make, which
// one is current and the blend between two, the camera stack a scene pushes and pops, the switches `CamEnable` sets,
// the shared target list, the human kept in view, the shake and slow motion. The script bindings
// (scripting/camera_bindings.h) and the scene player drive it; the player steps it after the characters, and the
// renderer draws view(). Platform-neutral and deterministic. Research: docs/research/camera.md

namespace coney::camera {

/// The kinds of camera Coney has (docs/research/camera.md#types).
enum class CameraKind : std::uint8_t {
    None,   ///< No camera yet.
    Follow, ///< The player's follow camera (type 2).
    Locked, ///< A locked camera (type 1).
    Scene,  ///< A scene's camera (type 4), which the scene player moves.
    Win,    ///< The Rumble win camera (`Cam_Win`), which circles the winner.
    Path,   ///< The scripts' path camera (type 3, `Cam_Spline`), which flies through its points.
};

/// A camera the manager knows: its kind and, for a locked one, its handle.
struct CameraRef {
    CameraKind kind = CameraKind::None;
    double handle = 0.0;
    friend bool operator==(const CameraRef&, const CameraRef&) = default;
};

/// The cameras of player 1.
class Cameras {
  public:
    /// `CamEnable`'s fourteen switches (docs/references/cameras.md#switch).
    static constexpr std::size_t kSwitches = 14;
    /// The switches Coney acts on.
    static constexpr std::size_t kSwitchStick = 0;       ///< The right stick and zoom buttons.
    static constexpr std::size_t kSwitchSprintZoom = 5;  ///< The follow camera's sprint zoom.
    static constexpr std::size_t kSwitchShakeView = 6;   ///< The shake's view offset.
    static constexpr std::size_t kSwitchLookBehind = 11; ///< The look-behind button.
    /// The shared target list holds at most this many humans (`0x005d91a8`).
    static constexpr std::size_t kTargetListSize = 4;
    /// The cameras' update a scene's end runs once, seconds, so the follow camera settles before the next frame.
    static constexpr float kSceneSettleSeconds = 0.17F;

    /// Finds where a human is by its script handle, for the human kept in view; nothing for a handle that names none.
    using Locator = std::function<std::optional<anim::Vec3>(double handle)>;

    /// The cameras with the switches as a level's camera reset leaves them.
    Cameras();

    /// Hands the manager player 1's follow camera (which must outlive it, or be detached with nullptr) and makes it
    /// current when nothing else is: the player's camera from the start, as `CameraMakeActive(MainCam, 0)` makes it.
    /// The level script runs before the player exists, so a `CfgFollowCamera` and `CamSetupFollow` made before are
    /// applied to it here (the configuration, then the reset).
    void attachFollow(FollowCamera* follow);
    /// The follow camera, or null.
    [[nodiscard]] FollowCamera* follow() const { return m_follow; }
    /// Sets how the manager finds humans by handle.
    void setLocator(Locator locator) { m_locate = std::move(locator); }
    /// Where a human stands and faces (feet, heading in degrees) by its script handle, for the win camera; nothing for
    /// a handle that names none.
    using Placer = std::function<std::optional<std::pair<anim::Vec3, float>>(double handle)>;
    /// Sets how the manager finds a human's placement.
    void setPlacer(Placer placer) { m_place = std::move(placer); }

    /// `CamSetupFollow(name, target)`: puts the follow camera on its target behind it (its reset) and returns its
    /// handle, `handle` the first time and the same one after (without a follow camera yet, it is reset when one is
    /// attached).
    /// @orig 0x0011bfa8 Camera_SetupFollow (unknown)
    [[nodiscard]] double setupFollow(double handle);
    /// The follow camera's handle, once `CamSetupFollow` gave it one.
    [[nodiscard]] std::optional<double> followHandle() const { return m_followHandle; }
    /// `CfgFollowCamera`: the follow camera's configuration (FollowCamera::configure()) and slow motion's factor.
    void configureFollow(const FollowSettings& settings, float slowMotion);

    /// `CameraCreateLocked`: keeps a locked camera under `handle`.
    void createLocked(double handle, const LockedCamera& camera);
    /// The locked camera with `handle`, or null.
    [[nodiscard]] const LockedCamera* locked(double handle) const;
    /// `CamDelete(camera)`: forgets the locked camera with `handle`; the shared kinds (follow, win) are kept. When it
    /// is current the follow camera is made current at once (**Coney choice**: what the original shows then is not
    /// traced).
    /// @orig 0x0011b888 Camera_Delete (unknown)
    void deleteCamera(double handle);
    /// `CameraCreateWin`: sets the one win camera up on the human `target` with `settings` and returns its handle,
    /// `handle` the first time and the same one after. It starts from where the target stands (through the placer)
    /// now, and again when it is made active, as the original starts a camera on activation (the winner is moved onto
    /// his flag between the two).
    /// @orig 0x0011c858 Camera_CreateWin (unknown)
    [[nodiscard]] double createWin(double handle, double target, const WinCameraSettings& settings);
    /// The win camera, once made.
    [[nodiscard]] const WinCamera* win() const { return m_win ? &*m_win : nullptr; }
    /// `CamSetFollowHeading(degrees)`: the follow camera swung round at once to view along `degrees` (0 facing +y),
    /// at its distance from the target's last feet (**Coney's reading** of the angle). Nothing before the follow
    /// camera has a target.
    /// @orig 0x0011c2f0 Camera_SetFollowHeading (unknown)
    void setFollowHeading(float degrees);

    /// `CameraSetClipping(camera, near, far)`: the camera's near clip and its own far clip, at most
    /// LockedCamera::kMaxFarClip; the follow camera keeps its near clip. Nothing for a handle that names no camera.
    /// @orig 0x0011bb98 Camera_SetClipping (unknown)
    void setClipping(double handle, float nearClip, float farClip);
    /// The follow camera's own far clip (the player camera's 115 until `CameraSetClipping`).
    [[nodiscard]] float followFarClip() const { return m_followFarClip; }
    /// `CameraGetActive`: the handle of the current camera; nothing for none or a scene's camera.
    /// @orig 0x0011b838 Camera_GetActiveHandle (unknown)
    [[nodiscard]] std::optional<double> activeHandle() const;
    /// `CamGetPos`: where the camera with `handle` is now; nothing for a handle that names no camera.
    /// @orig 0x0011b920 Camera_GetPositionByHandle (unknown)
    [[nodiscard]] std::optional<anim::Vec3> positionOf(double handle) const;
    /// `CamSetFollowPos`: the follow camera put at `position` at once (FollowCamera::placeAt()); nothing without one.
    void setFollowPosition(anim::Vec3 position);

    /// `CamSetupPoizo`: the one path camera started over from the view of the camera `start` names
    /// (PathCamera::setup()) and its handle, `handle` the first time and the same one after; nothing when `start` names
    /// no camera.
    [[nodiscard]] std::optional<double> setupPath(double handle, double start, float seconds, std::string onEnd,
                                                  float fieldOfView, float farClip);
    /// `CamAddPoizoPoint`: a point appended to the path; false without a path camera.
    /// @orig 0x0011cbb0 Camera_AddPoizoPoint (unknown)
    bool addPathPoint(const PathPoint& point);
    /// `CamAddPoizoPointCam`: the view of the camera `camera` names now, appended as a point; false without a path
    /// camera or for a handle that names no camera.
    /// @orig 0x0011cc68 Camera_AddPoizoPointCam (unknown)
    bool addPathPointFrom(double camera, float seconds, std::string onReach);
    /// The path camera, once made.
    [[nodiscard]] const PathCamera* path() const { return m_path ? &*m_path : nullptr; }
    /// The script functions the path camera reached since the last call (its points' and its end's), in order: the
    /// gameplay calls them after the step.
    [[nodiscard]] std::vector<std::string> takeFired() { return std::exchange(m_fired, {}); }

    /// `CameraMakeActive(camera, seconds)`: makes the camera with `handle` current, at once with 0 seconds or no
    /// current camera (which runs the follow camera's activation), otherwise through a blend from the view shown now.
    /// While a scene camera is current it replaces the camera on the stack instead. A handle that names no camera
    /// does nothing.
    /// @orig 0x0011ee08 Camera_MakeActive (Cam_ICamera.cpp)
    void makeActive(double handle, float seconds);
    /// `CameraReset(camera)`: the follow camera's reset (FollowCamera::reset()); nothing for a locked camera.
    /// @orig 0x0011bad8 Camera_ResetByHandle (unknown)
    void reset(double handle);
    /// `CamEnable(switch, on)`: sets a switch; 0 also turns the follow camera's stick on or off and 5 its sprint zoom.
    /// Switches out of range do nothing.
    /// @orig 0x0011de58 Camera_EnableFeature (unknown)
    void enable(std::size_t which, bool on);
    /// A switch's state.
    [[nodiscard]] bool enabled(std::size_t which) const { return which < kSwitches && m_switches.at(which); }
    /// `CamTarget(mode, camera, human)`: 0 adds the human to the shared list (false for NilHandle or a full list), 1
    /// removes it, 2 clears the list. Returns whether the list changed as the binding reports it.
    /// @orig 0x0011c270 Camera_TargetList (unknown)
    [[nodiscard]] bool target(int mode, double human);
    /// The shared target list. The follow camera follows player 1 whatever it holds, as the original does with one
    /// player (it falls back to its last target).
    [[nodiscard]] const std::vector<double>& targets() const { return m_targets; }
    /// `HuSwitchPlayer`: the cameras that followed human `from` follow `to`: its place in the shared target list and
    /// the kept-in-view human (the follow camera follows player 1, whoever the pad drives).
    /// @orig 0x00122248 Camera_SwitchTarget (unknown)
    void switchTarget(double from, double to);
    /// `CamSetSecondary(object, range)`: a human to keep in view in place of auto-follow; NilHandle (0) ends it.
    /// @orig 0x0011dcf0 Camera_SetFollowSecondary (unknown)
    void setSecondary(double human, float range);
    /// Where the human kept in view is, through the locator; nothing when there is none.
    [[nodiscard]] std::optional<anim::Vec3> secondaryPoint() const;
    /// Its range (0 for none).
    [[nodiscard]] float secondaryRange() const { return m_secondaryRange; }
    /// `CamSetFollowZoom(preset)` and `CamSetFollowAngle(degrees)` on the follow camera.
    void setFollowZoom(FollowZoom preset);
    void setFollowAngle(float degrees);
    /// `CamSetSplitMode(mode)`: the two-player split-screen layout, 0 or 1 (any other value is ignored). **Coney
    /// stand-in**: Coney has one player and one view, so the layout is kept and not applied.
    /// @orig 0x0011e0a8 Camera_SetSplitMode (unknown)
    void setSplitMode(std::uint32_t mode) {
        if (mode <= 1) {
            m_splitMode = mode;
        }
    }
    /// The split-screen layout.
    [[nodiscard]] std::uint32_t splitMode() const { return m_splitMode; }
    /// `CamAssignRevCamButton(button)`: the button that shows the reverse (look-behind) camera (`0x0050b230`).
    /// **Coney stand-in**: Coney has no reverse camera yet, so the button is kept.
    /// @orig 0x0011e198 Camera_AssignReverseButton (unknown)
    void setReverseButton(std::uint32_t button) { m_reverseButton = button; }
    /// The reverse camera's button.
    [[nodiscard]] std::uint32_t reverseButton() const { return m_reverseButton; }

    /// A scene with a camera starts: the current camera (a blend's destination) is pushed on the stack and the scene
    /// camera, showing `view`, made current at once.
    void beginScene(const CameraView& view);
    /// The scene camera's view this update.
    void setSceneView(const CameraView& view);
    /// The scene ends: the camera on the stack is popped, reset and made current over `blendSeconds` (0 or below: a
    /// cut), the scene camera released, and the cameras updated once with kSceneSettleSeconds.
    void endScene(float blendSeconds);
    /// The cameras the scenes will return to, oldest first.
    [[nodiscard]] const std::vector<CameraRef>& stack() const { return m_stack; }

    /// Starts a shake of `level` (1-3, 0 stops) on the current camera, weaker while the combat camera is on.
    void shake(int level);
    /// The shake and its pad rumble.
    [[nodiscard]] const CameraShake& shaking() const { return m_shake; }
    /// Slow motion, which the player's animation events drive and `CfgFollowCamera` configures.
    [[nodiscard]] SlowMotion& slowMotion() { return m_slowMotion; }
    [[nodiscard]] const SlowMotion& slowMotion() const { return m_slowMotion; }

    /// One update of `seconds`, after the characters' step: the follow camera's own update when it is current or a
    /// blend's destination (otherwise it only notes where `target` is), the blend toward its destination (which becomes
    /// current when the time is up), the shake; then the current view. `mesh` may be null and must outlive the next
    /// endScene().
    /// @orig 0x0011e878 Cameras_Update (Cam_ICamera.cpp)
    void update(const FollowTarget& target, std::uint8_t rawRightX, std::uint8_t rawRightY,
                const raycast::CollisionMesh* mesh, float seconds);

    /// The current camera (the blend's destination while blending).
    [[nodiscard]] CameraRef current() const { return m_current; }
    /// Whether a blend is under way.
    [[nodiscard]] bool blending() const { return m_blend.has_value(); }
    /// What the current camera shows, after the last update (with the shake's offset while switch 6 is on).
    [[nodiscard]] const CameraView& view() const { return m_view; }

  private:
    // The view of the camera `ref` names now, without the shake.
    [[nodiscard]] CameraView viewOf(CameraRef ref) const;
    // The camera a handle names, or none.
    [[nodiscard]] std::optional<CameraRef> find(double handle) const;
    // Starts the win camera on its target where it stands now; nothing when the target is not found.
    void startWin();
    // Makes `ref` current at once, running the follow camera's activation.
    void setCurrent(CameraRef ref);
    // Starts a blend from the view shown now to `ref`, or cuts when there is nothing to blend from.
    void switchTo(CameraRef ref, float seconds);

    FollowCamera* m_follow = nullptr;
    std::optional<double> m_followHandle;
    std::optional<FollowSettings> m_pendingSettings; // a CfgFollowCamera made before the follow camera was attached
    std::map<double, LockedCamera> m_locked;
    float m_followFarClip = kPlayerCameraLens.farClip;
    std::optional<WinCamera> m_win;
    std::optional<PathCamera> m_path;
    double m_pathHandle = 0.0;
    std::vector<std::string> m_fired; // the path camera's functions reached, for takeFired()
    double m_winHandle = 0.0;         // the win camera's handle once made
    double m_winTarget = 0.0;
    WinCameraSettings m_winSettings;
    Placer m_place;
    CameraRef m_current;
    std::optional<CameraBlend> m_blend; // toward m_current
    std::vector<CameraRef> m_stack;
    CameraView m_sceneView;
    CameraView m_view;
    std::array<bool, kSwitches> m_switches{};
    std::vector<double> m_targets;
    double m_secondary = 0.0;
    float m_secondaryRange = 0.0F;
    std::uint32_t m_splitMode = 0;
    std::uint32_t m_reverseButton = 0;
    Locator m_locate;
    CameraShake m_shake;
    SlowMotion m_slowMotion;
    // The last update's target and mesh, for the scene end's settling update.
    std::optional<FollowTarget> m_lastTarget;
    const raycast::CollisionMesh* m_lastMesh = nullptr;
};

} // namespace coney::camera
