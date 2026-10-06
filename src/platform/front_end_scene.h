// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string_view>

#include "animation/anim_math.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "gamemodes/front_end_scene.h"
#include "graphics/level_lighting.h"
#include "platform/placed_objects.h"
#include "platform/render_engine.h"
#include "platform/scene_lighting.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "world/level_object.h"
#include "world/sector_budget.h"
#include "world_objects/object_list.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

namespace coney::platform {

/// Where the front-end scene finds its dynamic objects: the flow's spawn records and object types (both null: none).
/// Both must outlive the scene.
struct FrontEndObjectSource {
    const world_objects::SpawnRecords* records = nullptr;
    const world_objects::ObjectTypes* types = nullptr;
};

/// The front end's 3D background (docs/research/frontend.md#background): `level100`'s streamed worlds and level file,
/// streamed one decision a step around the camera and drawn by the world renderer, cleared to black (the background
/// colour `LevelFlow_StartFrontEnd` sets), with the menus' 2D pass and fade drawn over them before the frame is shown.
///
/// **Coney's stand-in camera** until the scene player (src/scenes, being written) plays `WonderWheel_100`, whose
/// `camera01` track is the real view: the track's first pose, (462.60, −122.35, −187.93) in the game's axes, with its
/// lens (field of view 54.43° across, near 0.5, far 150), turned to the wheel's hub at (515.51, −68.89, −188.67) and
/// then kHubYawOffsetDegrees to the left, so the hub sits right of centre where the runtime picture has the wheel (its
/// outline across logical x 335-615 of 640). The camera does not move, as at runtime.
///
/// **The dynamic objects** (the wheel, its carts and neon signs, spawned by `level100.lua`'s `ObjSpawn`): every live
/// spawn record of the objects source is drawn with its type's model (PlacedObjects), at the pose a scene last gave
/// it (setObjectPose()) or else at its record's, and shown or hidden by messages 0x12 and 0x13 (objectMessage()). A
/// record becomes live when its handle is resolved (`SceneAddObject`); the 70 m streaming that would also spawn one
/// is not Coney's yet, and the wheel stands 75 m from the camera, beyond it.
///
/// **Lighting** is the light manager as it starts, before any script sets it: the world ambient at the brightness
/// alone, and the fog black (kBackground). `level100.lua`'s own lights are not run yet.
class FrontEndWorldScene final : public FrontEndScene {
  public:
    /// The scene camera's first pose and the hub it faces, in the game's axes (z up).
    static constexpr float kCameraX = 462.60F;
    static constexpr float kCameraY = -122.35F;
    static constexpr float kCameraZ = -187.93F;
    static constexpr float kHubX = 515.51F;
    static constexpr float kHubY = -68.89F;
    static constexpr float kHubZ = -188.67F;
    /// The scene camera's lens.
    static constexpr float kFieldOfViewDegrees = 54.43F;
    static constexpr float kNearClip = 0.5F;
    static constexpr float kFarClip = 150.0F;
    /// Coney's turn to the left of the hub, so the world's "WONDER WHEEL" sign lands where the runtime picture has it,
    /// near logical (410, 217) of 640 × 448.
    static constexpr float kHubYawOffsetDegrees = 10.7F;
    /// The background (and fog) colour the front end sets.
    static constexpr graphics::Rgba kBackground = graphics::kBlack;

    /// Loads level `name`'s worlds and level file from `wad` into a Sector Pool of its own (the level owns it until it
    /// is unloaded, so a level loaded after the front end starts from an empty pool), and preloads the parts round the
    /// camera. `print` gets the load's summary. Fails as loadLevelScenery() does.
    [[nodiscard]] static std::expected<std::unique_ptr<FrontEndWorldScene>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name,
           const std::function<void(std::string_view)>& print, FrontEndObjectSource objects = {});

    /// One streaming decision round the camera and the visibility pass, then the dynamic objects brought up to date
    /// with the live spawn records: a new one placed, one no longer live dropped.
    void update(std::uint64_t nowMs) override;

    /// The world through the camera, the dynamic objects between its two streamed worlds, then `overlay`, then the
    /// present.
    void render(const RenderTime& time, const std::function<void()>& overlay) override;

    /// Puts object `handle` at `position` turned by `rotation` (the game's axes), as a scene's track does each step;
    /// the pose holds until the next. Drawn while its record is live.
    void setObjectPose(double handle, anim::Vec3 position, anim::Quat rotation);
    /// A message sent to object `handle`: `simple_object`'s 0x12 shows it and 0x13 hides it
    /// (docs/research/objects.md#simple-object); others do nothing here.
    void objectMessage(double handle, int message);

    /// The view the scene is drawn through.
    [[nodiscard]] const WorldView& view() const { return m_view; }
    /// The dynamic objects; null when the Object List did not load.
    [[nodiscard]] const PlacedObjects* objects() const { return m_objects.get(); }

  private:
    // A pose a scene gave an object.
    struct ObjectPose {
        anim::Vec3 position;
        anim::Quat rotation;
    };

    explicit FrontEndWorldScene(RenderEngine& engine);

    // Places every live record's object (at its scene pose, or its record's) and drops the objects whose records are
    // gone or no longer live.
    void syncObjects();

    RenderEngine& m_engine;
    world::SectorBudget m_budget{world::kSectorPoolSize}; // before the worlds charged to it
    std::unique_ptr<WorldSet> m_set;
    std::unique_ptr<world::LevelObject> m_level;
    graphics::LevelLighting m_lighting;
    SceneLighting m_sceneLighting; // after the lighting it reads, before the renderer that draws with it
    WorldRenderer m_renderer;
    WorldView m_view;
    float m_pendingDistance = 0.0F;
    FrontEndObjectSource m_source;
    std::unique_ptr<world_objects::ObjectList> m_objectList; // before the objects that read it
    std::unique_ptr<PlacedObjects> m_objects;
    std::map<double, ObjectPose> m_poses;
    std::set<double> m_hidden; // the objects a message 0x13 hid
};

/// The stand-in camera's view: kCameraX/Y/Z turned to the hub and kHubYawOffsetDegrees left, in RenderWare's axes,
/// with the lens's view window on the 4:3 picture.
[[nodiscard]] WorldView frontEndSceneView();

} // namespace coney::platform
