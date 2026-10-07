// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "animation/anim_pose.h"
#include "scenes/scene_record.h"

// What a playing scene asks of the game around it: the humans and objects bound to its roles, the camera, the lights,
// the screen, sound, particles, the pads' rumble and the brains. The scene player (scenes::SceneSystem) is pure and
// calls these hooks; the play mode implements them (src/platform). Every hook does nothing (or answers "ready") by
// default, so a scene plays to its end with no game around it: the headless tests run it that way.
// Research: docs/research/scenes.md#behaviour

namespace coney::scenes {

/// `ScreenQueueEffect`'s types a scene queues on every player's view (docs/research/scenes.md#starting).
enum class ScreenEffect : std::uint8_t {
    FadeIn = 0,       ///< From black over the given time.
    FadeOut = 1,      ///< To black; 0 s is black at once.
    LetterboxIn = 2,  ///< The bars close in (1.5 s, at once for a chained scene).
    LetterboxOut = 3, ///< The bars open again (1.5 s).
    EndBlurPulse = 5, ///< Ends the blur pulse, at once, as a scene with a camera starts.
};

/// A camera's lens as a scene sets it: the definition's, then each lens event's (type 26).
struct SceneLens {
    float fieldOfView = 60.0F; ///< Degrees.
    float nearClip = 0.1F;
    float farClip = 100.0F;
};

/// A light as a scene sets it: the definition, then each light event's (type 30) colour, range and cone.
struct SceneLight {
    std::uint32_t kind = 0;
    std::array<float, 3> colour{};
    float coneDegrees = 0.0F;
    float range = SceneLightDef::kDefaultLightRange;
};

/// A bound human's state for one update of the scene: the clip's pose, where its feet are and where it faces, in the
/// world. The scene drives it from its role's clip: root motion moves and turns it, the clip's events 21 and 22 put it
/// at its marks (docs/research/scenes.md#humans).
struct RoleFrame {
    std::size_t role = 0;
    anim::Pose pose;
    anim::Vec3 feet;      ///< World, game axes.
    float heading = 0.0F; ///< Radians, 0 facing +y.
};

/// The game around a playing scene. Handles are the scripts' (`HuCreate`, `ObjSpawn`); 0 is `NilHandle` and never
/// reaches a hook.
class SceneHost {
  public:
    SceneHost() = default;
    SceneHost(const SceneHost&) = delete;
    SceneHost& operator=(const SceneHost&) = delete;
    SceneHost(SceneHost&&) = delete;
    SceneHost& operator=(SceneHost&&) = delete;
    virtual ~SceneHost() = default;

    // ---- Humans ----

    /// `GoalJoinCinematic`: `human` joins role `role` of `scene`; it walks to `start` at `gait` before the scene
    /// starts. A pad-controlled human loses its control here (`BrDead(brain, 1)`).
    virtual void humanJoin(double /*human*/, std::uint32_t /*scene*/, std::size_t /*role*/, const ScenePose& /*start*/,
                           int /*gait*/) {}
    /// Whether `human` exists with its character and model loaded; the start waits until every bound human is.
    [[nodiscard]] virtual bool humanReady(double /*human*/) { return true; }
    /// Whether `human` stands at `mark` (the start waits for that while its delay lasts).
    [[nodiscard]] virtual bool humanAtMark(double /*human*/, const ScenePose& /*mark*/) { return true; }
    /// Whether `human` is free to be taken in at the start: not grabbing, grabbed, mounting or mounted. A human in a
    /// pair is left out of the scene (docs/research/scenes.md#humans).
    [[nodiscard]] virtual bool humanFree(double /*human*/) { return true; }
    /// The scene takes `human` over (`+0x280` = the scene, its brain's actions cleared, its clip pushed).
    virtual void humanEnterScene(double /*human*/, std::size_t /*role*/) {}
    /// The scene drives `human` this update.
    virtual void humanPose(double /*human*/, const RoleFrame& /*frame*/) {}
    /// `human`'s clip has ended and the scene no longer drives it; it stays where the clip left it.
    virtual void humanExitScene(double /*human*/) {}
    /// The scene has ended for `human`: its join goal is popped, which gives a player control back. `endPose` (world)
    /// is where a skipped scene puts it; nothing when the scene played out.
    virtual void humanRelease(double /*human*/, const std::optional<ScenePose>& /*endPose*/) {}
    /// Every brain suspended (`freeze`) or resumed, including any a script had suspended.
    virtual void suspendBrains(bool /*suspended*/) {}

    // ---- Objects and lights ----

    /// Whether the object `object` is loaded; the start waits until every bound object is.
    [[nodiscard]] virtual bool objectReady(double /*object*/) { return true; }
    /// The scene moves `object` to `pose` (world).
    virtual void objectPose(double /*object*/, const ScenePose& /*pose*/) {}
    /// Track events 24 and 25 send messages `0x12` and `0x13` to the track's object (meaning not traced).
    virtual void objectMessage(double /*object*/, int /*message*/) {}
    /// The scene is done with `object`: it goes back to the object manager.
    virtual void objectRelease(double /*object*/) {}
    /// Light `index` of the scene is made or changed (world pose).
    virtual void lightSet(std::size_t /*index*/, const ScenePose& /*pose*/, const SceneLight& /*light*/) {}
    /// Light `index` is released.
    virtual void lightRelease(std::size_t /*index*/) {}

    // ---- Camera ----

    /// The scene camera is made, the current camera pushed and the scene camera made current with no blend.
    virtual void cameraBegin(const ScenePose& /*pose*/, const SceneLens& /*lens*/) {}
    /// The scene camera this update (world pose, the camera's +y forward and +z up).
    virtual void cameraPose(const ScenePose& /*pose*/, const SceneLens& /*lens*/) {}
    /// The pushed camera is popped and made current over `blendSeconds` (0 or below: a cut), the scene camera
    /// released and the cameras updated once with 0.17 s.
    virtual void cameraEnd(float /*blendSeconds*/) {}
    /// The world around `centre` is preloaded within `radius`, with `pak` (`<scene>.pak`, `<scene>_end.pak`) when it
    /// exists (docs/research/level-loading.md#preload).
    virtual void preloadWorld(anim::Vec3 /*centre*/, float /*radius*/, std::string_view /*pak*/) {}

    // ---- Screen, sound and pads ----

    /// `ScreenQueueEffect(type, seconds)` on every player's view.
    virtual void screenEffect(ScreenEffect /*type*/, float /*seconds*/) {}
    /// Track event 74: a fade out (`out`) or in through `rgb` (low 24 bits) over `seconds`.
    virtual void colouredFade(bool /*out*/, std::uint32_t /*rgb*/, float /*seconds*/) {}
    /// Track event 41's caption control for the scene named `scene`: 0 shows its next caption, 4 and 5 clear it, 6
    /// sets a flag first; an end of the caption (a fade out, a skip) is 4.
    virtual void caption(std::string_view /*scene*/, int /*command*/) {}
    /// The scene soundtrack `hash` (a 32,250 Hz stereo stream) is prepared without starting, as the scene loads; the
    /// previous scene's soundtrack stops (docs/research/sound.md#scene-sound).
    virtual void soundtrackPrepare(std::uint32_t /*hash*/) {}
    /// Event 13 (on a role's clip or a track) starts the prepared soundtrack, whichever scene's it is.
    virtual void soundtrackStart() {}
    /// Whether a cinematic's start may go on as far as the soundtrack goes: the prepared one is buffered, or none is
    /// prepared or waiting for a stream pair (docs/research/sound.md#scene-sound).
    [[nodiscard]] virtual bool soundtrackReady() { return true; }
    /// The soundtrack stops: a skipped scene's end, a cinematic given up.
    virtual void soundtrackStop() {}
    /// Events 14 and 71: a sound by name hash on the track's object or human (`object`), or with none.
    virtual void sound(std::uint32_t /*hash*/, std::optional<double> /*object*/) {}
    /// Track event 33: a particle effect named `name` at `pose` (world).
    virtual void particle(std::string_view /*name*/, const ScenePose& /*pose*/) {}
    /// Track event 76: rumble on every player's pad at `strength` (0-255).
    virtual void rumble(int /*strength*/) {}
    /// Writes one line (with its newline) to the log.
    virtual void log(std::string_view /*line*/) {}
};

} // namespace coney::scenes
