// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

// The world objects a level's spawn records make, as far as drawing them goes: which records have an object now (the
// streaming round the camera), each object's pose and tint, and the one class with behaviour of its own that Coney
// runs, the objective marker (`dyn_objective` and its column). The drawing rules that need the camera and the model
// (the size cull, the `ObjShow` distance, the alpha floor) are the pure functions below; src/platform draws.
// Research: docs/research/objects.md#dynamic-objects, docs/research/objects.md#objective-markers,
// docs/research/objects.md#tint

namespace coney::world_objects {

/// The class of the objective markers' disc (script type 45).
inline constexpr std::string_view kObjectiveClass = "dyn_objective";
/// The column's own model (`sub_objective_column`'s init sets it by this hash).
inline constexpr std::uint32_t kColumnModelHash = 0x1f3b85eaU;

/// The column's colour word `0xRRGGBBAA`, alpha 0 (the fade supplies it), for a disc whose model hash is
/// `discModelHash`: yellow-gold, red, purple, or white for any other.
/// @orig 0x003e98e8 dyn_objective_Init (unknown)
[[nodiscard]] std::uint32_t columnColourFor(std::uint32_t discModelHash);

/// An objective marker's state: the disc's turn about the vertical and the fade the disc and its column share.
/// Stepped once per update (2 ticks, 1/30 s).
///
/// Research: docs/research/objects.md#objective-markers
/// @orig 0x003e9b08 dyn_objective_Update (unknown)
class ObjectiveMarker {
  public:
    /// The disc's angular velocity about the world's z axis, radians a second (90°).
    static constexpr float kTurnPerSecond = std::numbers::pi_v<float> / 2.0F;
    /// The update's length, seconds (its interval of 2 ticks).
    static constexpr float kUpdateSeconds = 1.0F / 30.0F;
    /// What the tint's alpha byte moves each update towards 255 (shown) or 0 (hidden).
    static constexpr int kFadeStep = 8;

    /// `ObjShow` / `ObjHide` (message 0x0a with 1 or 0): shown or hidden, passed on to the column.
    /// @orig 0x003e9828 ObjectiveMarker_SetShown (unknown)
    void setShown(bool shown) { m_shown = shown; }
    /// The destroy message (0x15, `ObjDestroy(object, true)`): dying and hidden, so it fades out and goes.
    void setDying() {
        m_dying = true;
        m_shown = false;
    }

    /// One update: the drawn alpha takes the last update's (`+0xcc` copied to `+0xc8`), the disc turns, and the alpha
    /// steps kFadeStep towards its target. Returns true when the marker is gone (dying, its alpha at 0).
    /// @orig 0x00395b70 WorldObject_Update (unknown)
    bool update();

    /// The disc's turn about the vertical so far, radians in [0, 2π).
    [[nodiscard]] float angle() const { return m_angle; }
    /// The alpha drawn this update (`+0xc8`) and the one the update set (`+0xcc`).
    [[nodiscard]] std::uint8_t drawnAlpha() const { return m_drawnAlpha; }
    [[nodiscard]] std::uint8_t alpha() const { return m_alpha; }
    [[nodiscard]] bool shown() const { return m_shown; }
    [[nodiscard]] bool dying() const { return m_dying; }

  private:
    float m_angle = 0.0F;
    // **Coney's stand-in**: a new marker starts at alpha 0 (the runtime saw 0 before the first `ObjShow`; the page
    // does not say what its init sets), so it never shows before it is shown.
    std::uint8_t m_alpha = 0;
    std::uint8_t m_drawnAlpha = 0;
    bool m_shown = false; // the init hides it
    bool m_dying = false;
};

/// One world object to draw this step.
struct ObjectDraw {
    double handle = 0.0;         ///< The spawn record's handle (the column carries its disc's).
    std::uint32_t modelHash = 0; ///< The Object List record of its model.
    anim::Vec3 position;         ///< Game axes, z up.
    anim::Quat rotation;
    /// The tint word `0xRRGGBBAA`: multiplies the model's colours, its alpha the model's opacity, already scaled by the
    /// class's fade and the fade-in.
    std::uint32_t tint = 0xFFFFFFFFU;
    float fadeDistance = 0.0F;   ///< `ObjShow`'s distance (`+0x138`, the parent's for an attached object); 0 none.
    bool sizeCullExempt = false; ///< `+0x124` set: never size-culled.
    bool column = false;         ///< An objective marker's column: drawn translucent (Coney's stand-in blend).
};

/// The alpha under which an object is not drawn.
inline constexpr int kMinDrawnAlpha = 10;

/// The size cull's factor for an object of bounding `radius` at squared camera distance `distanceSq`: 0 while
/// radius / distanceSq is under 0.0004, rising to 1 at 0.0005.
/// @orig 0x0017fd78 ObjectRender_Draw (unknown)
[[nodiscard]] float sizeFade(float radius, float distanceSq);

/// The `ObjShow` distance's factor (`+0x138`): with a distance above 0 and the camera farther than distance − 2 m,
/// (distance − camera distance) / 2, clamped at 0; otherwise 1.
/// @orig 0x0017fa80 ObjectRender_ApplyFadeDistance (unknown)
[[nodiscard]] float showDistanceFade(float fadeDistance, float cameraDistance);

/// The fade-in of a new instance over its first second: `msSinceAppeared` / 1000, at most 1. **Coney's reading**: the
/// page gives the second, not the curve; linear.
[[nodiscard]] float appearFade(std::uint32_t msSinceAppeared);

/// A tint word's alpha byte multiplied by `factor` (clamped to 0-1).
[[nodiscard]] std::uint32_t scaleTintAlpha(std::uint32_t tint, float factor);

/// Where a held object hangs from its human: a pose bone and the object's local pose in that bone's frame (object
/// `+0x6d`, `+0x10`, `+0x20`).
struct HeldAttachment {
    std::size_t bone = 0; ///< The pose bone (25 the right hand, 19 the left).
    anim::Vec3 position;  ///< In the bone's frame, metres.
    anim::Quat rotation;
};

/// The pick-up clip's events that put an object in the hand: type 9 and type 0x36 (the left-hand clips).
inline constexpr std::uint16_t kTakeEvent = 9;
inline constexpr std::uint16_t kTakeEventLeft = 0x36;

/// The take (message `0x1b`) from the pick-up clip `clip`: its first type-9 (or 0x36) event's bone, its position ×
/// `humanScale`, then slid along the object's own y by `grip` (`CfgObj` `+0x70`, the local rotation applied to
/// `(0, grip, 0)`), its rotation as is. Nothing when the clip has no such event.
/// @orig 0x003fe490 melee_weapon_HandleMessage (unknown)
[[nodiscard]] std::optional<HeldAttachment> heldAttachment(const anim::AnimClip& clip, float humanScale, float grip);

/// A pose in the game's axes.
struct WorldPose {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// A held object's world pose (`Obj_GetWorldPose`): the human's transform (at `feet`, turned by `heading` about z) ∘
/// the bone's (`bone`, character space, its position × `boneScale`) ∘ the attachment's local pose. Rotations compose
/// `qa · qb`, positions `pa + qa · pb`; nothing is scaled but the bone's position.
/// @orig 0x003a19b0 Obj_GetWorldPose (unknown)
[[nodiscard]] WorldPose heldWorldPose(anim::Vec3 feet, float heading, const anim::Mat34& bone, float boneScale,
                                      const HeldAttachment& held);

/// The world objects of a level: an object for each spawn record that streaming or a binding brought in, its draw,
/// and the objective markers' updates. Stepped on the fixed 1/30 s step after the scripts.
///
/// **Coney's stand-ins** for what the pages leave open or Coney does not model yet: every record within reach comes
/// in on the same step (the original spawns one a call); the streaming-out distance is the draw distance + 10 m (the
/// first camera's vtable `+0x214` is untraced); a record a binding resolved (live) or a scene pinned is drawn whatever
/// its distance and zone; `ObjHide` hides a plain object. An object in a hand is left out here: its holder draws it
/// (heldWorldPose()).
///
/// Research: docs/research/objects.md#streaming, docs/research/objects.md#objective-markers
class ObjectTasks {
  public:
    /// The distance from the camera under which a record comes in, metres.
    static constexpr float kSpawnDistance = 70.0F;
    /// Beyond the draw distance by this much, a record that is not live or pinned goes out.
    static constexpr float kStoreMargin = 10.0F;

    /// What a step needs beyond the records.
    struct StepView {
        anim::Vec3 camera;                  ///< The nearest camera, game axes.
        float drawDistance = 0.0F;          ///< The scenery's draw distance.
        std::uint32_t elapsedMs = 33;       ///< The step's game time.
        std::function<bool(double)> inHand; ///< Whether an object is in a hand (null: none is).
    };

    /// One step: the records in reach brought in and those out of it dropped, each marker updated (a dying one at
    /// alpha 0 removed from `records` for good), and the draw list made.
    /// @orig 0x00399d88 ObjectTaskManager_UpdateSpawns (unknown)
    void step(SpawnRecords& records, const ObjectTypes& types, const StepView& view);

    /// This step's objects to draw, in the records' order (a marker's disc, then its column).
    [[nodiscard]] const std::vector<ObjectDraw>& draws() const { return m_draws; }
    /// The marker of object `handle`; null when it is not a marker in the world.
    [[nodiscard]] const ObjectiveMarker* marker(double handle) const;
    /// Whether object `handle` is in the world now (streamed in).
    [[nodiscard]] bool inWorld(double handle) const { return m_tasks.contains(handle); }
    /// How many objects are in the world now.
    [[nodiscard]] std::size_t count() const { return m_tasks.size(); }
    /// Drops every object (the level is unloaded).
    void clear() {
        m_tasks.clear();
        m_draws.clear();
    }

  private:
    // One object in the world.
    struct Task {
        std::uint32_t msSinceAppeared = 0;
        std::optional<ObjectiveMarker> marker;
    };

    // Whether `record` belongs in the world this step; `present` whether it was last step.
    [[nodiscard]] bool wanted(const SpawnRecords& records, const SpawnRecord& record, const StepView& view,
                              bool present) const;
    // Adds the draws of `record`'s object.
    void addDraws(const SpawnRecord& record, const ObjectType& type, const Task& task);

    std::map<double, Task> m_tasks;
    std::vector<ObjectDraw> m_draws;
};

} // namespace coney::world_objects
