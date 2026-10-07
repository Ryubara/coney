// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>

#include "animation/anim_math.h"
#include "core/error.h"
#include "core/interpolation.h"
#include "effects/screen_tint.h"
#include "fileio/wad.h"
#include "gamemodes/front_end_scene.h"
#include "graphics/level_lighting.h"
#include "platform/placed_objects.h"
#include "platform/render_engine.h"
#include "platform/scene_lighting.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "scenes/scene_host.h"
#include "world/level_object.h"
#include "world/sector_budget.h"
#include "world_objects/object_list.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

namespace coney::platform {

/// Where the front-end scene finds its dynamic objects: the flow's spawn records and object types (both null: none).
/// Both must outlive the scene.
struct FrontEndObjectSource {
    world_objects::SpawnRecords* records = nullptr;
    const world_objects::ObjectTypes* types = nullptr;
};

/// The front end's 3D background (docs/research/frontend.md#background): `level100`'s streamed worlds and level file,
/// streamed one decision a step around the camera and drawn by the world renderer, cleared to black (the background
/// colour `LevelFlow_StartFrontEnd` sets), with the menus' 2D pass and fade drawn over them before the frame is shown.
///
/// **The scenes' host** (scenes::SceneHost): `level100.lua`'s `WonderWheelAnim` plays the looping scene
/// `WonderWheel_100` on the front end's scene system, which this world hosts. The scene's camera is the view: it is
/// made current as the scene starts (cameraBegin()), its track moves it (cameraPose()) and it goes as the scene ends.
/// Without it the current camera is the script's (`CameraCreateLocked("Black", ...)`), about 550 m from the wheel and
/// past its far clip, so the frame is the black background alone (frontend.md#background).
///
/// **The dynamic objects** (the wheel, its carts and neon signs, spawned by `level100.lua`'s `ObjSpawn`): every live
/// spawn record of the objects source is drawn with its type's model (PlacedObjects), at the pose the scene's track
/// last gave it (objectPose(), blended between the last two steps) or else at its record's, and shown or hidden by
/// messages 0x12 and 0x13 (objectMessage()). A record becomes live when `SceneAddObject` resolves its handle; the
/// scene releasing an object unpins its record (docs/research/scenes.md#ending). The 70 m streaming that would also
/// spawn one is not Coney's yet.
///
/// **Lighting** is the front-end level's light manager, which the scripts' lighting bindings fill (lighting()):
/// `global.lua`'s lights for level 100, with the fog black (kBackground). The objects are drawn with the world
/// renderer's render states, without per-object lights (PlacedObjects).
///
/// **Coney's choices**: when the scene camera starts, the world round it is preloaded at once (the original streams it
/// during the menus' 1.5 s fade in); a scene's own lights are counted, not made.
class FrontEndWorldScene final : public FrontEndScene, public scenes::SceneHost {
  public:
    /// The background (and fog) colour the front end sets.
    static constexpr graphics::Rgba kBackground = graphics::kBlack;
    /// The radius the world is preloaded in round the scene camera when it starts: the scene's far clip, 150 m.
    static constexpr float kPreloadRadius = 150.0F;

    /// Loads level `name`'s worlds and level file from `wad` into a Sector Pool of its own (the level owns it until it
    /// is unloaded, so a level loaded after the front end starts from an empty pool). `print` gets the load's summary.
    /// Fails as loadLevelScenery() does.
    [[nodiscard]] static std::expected<std::unique_ptr<FrontEndWorldScene>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name,
           const std::function<void(std::string_view)>& print, FrontEndObjectSource objects = {});

    ~FrontEndWorldScene() override;
    FrontEndWorldScene(const FrontEndWorldScene&) = delete;
    FrontEndWorldScene& operator=(const FrontEndWorldScene&) = delete;
    FrontEndWorldScene(FrontEndWorldScene&&) = delete;
    FrontEndWorldScene& operator=(FrontEndWorldScene&&) = delete;

    /// One step after the scenes' update: the camera's and objects' new poses become the newest, then (with a scene
    /// camera) one streaming decision round it and the visibility pass, and the dynamic objects brought up to date
    /// with the live spawn records.
    void update(std::uint64_t nowMs) override;

    /// The world through the scene camera between the last two steps, the dynamic objects between its two streamed
    /// worlds, then `overlay`, then the present; without a scene camera the black background and `overlay` alone.
    void render(const RenderTime& time, const std::function<void()>& overlay) override;

    [[nodiscard]] scenes::SceneHost* sceneHost() override { return this; }
    [[nodiscard]] graphics::LevelLighting* lighting() override { return &m_lighting; }
    [[nodiscard]] effects::ScreenTint* tint() override { return &m_tint; }

    // ---- scenes::SceneHost ----
    void objectPose(double object, const scenes::ScenePose& pose) override;
    void objectMessage(double object, int message) override;
    void objectRelease(double object) override;
    void lightSet(std::size_t index, const scenes::ScenePose& pose, const scenes::SceneLight& light) override;
    void cameraBegin(const scenes::ScenePose& pose, const scenes::SceneLens& lens) override;
    void cameraPose(const scenes::ScenePose& pose, const scenes::SceneLens& lens) override;
    void cameraEnd(float blendSeconds) override;
    void log(std::string_view line) override { m_print(line); }

    /// Whether the scene camera is current (a scene with a camera is playing).
    [[nodiscard]] bool cameraActive() const { return m_camera.has_value(); }
    /// The view through the scene camera as of the newest step; nothing while no scene camera is current.
    [[nodiscard]] std::optional<WorldView> view() const;
    /// The dynamic objects; null when the Object List did not load.
    [[nodiscard]] const PlacedObjects* objects() const { return m_objects.get(); }
    /// The newest pose a scene gave object `handle` (the game's axes); nothing when none did.
    [[nodiscard]] std::optional<scenes::ScenePose> objectPoseOf(double handle) const;
    /// The scene lights set so far (counted, not made).
    [[nodiscard]] std::uint64_t sceneLights() const { return m_sceneLights; }

  private:
    // The scene camera at a step.
    struct CameraState {
        scenes::ScenePose pose;
        scenes::SceneLens lens;
    };

    FrontEndWorldScene(RenderEngine& engine, std::function<void(std::string_view)> print);

    // The view through the scene camera `alpha` of the way between its last two steps (one must be current).
    [[nodiscard]] WorldView viewAt(float alpha) const;
    // Drops the objects whose records are gone or no longer live, and keeps every live one's pose for this step.
    void syncObjects();
    // Places every kept object at its pose `alpha` of the way between the last two steps.
    void placeObjects(float alpha);

    RenderEngine& m_engine;
    std::function<void(std::string_view)> m_print;
    world::SectorBudget m_budget{world::kSectorPoolSize}; // before the worlds charged to it
    std::unique_ptr<WorldSet> m_set;
    std::unique_ptr<world::LevelObject> m_level;
    graphics::LevelLighting m_lighting;
    effects::ScreenTint m_tint;
    std::optional<std::uint64_t> m_lastStepMs; // the game time of the last update(), which the tint's blend steps from
    SceneLighting m_sceneLighting;             // after the lighting it reads, before the renderer that draws with it
    WorldRenderer m_renderer;
    float m_pendingDistance = 0.0F;
    std::optional<Interpolated<CameraState>> m_camera; // the scene camera at the last two steps; none: the script's
    std::optional<CameraState> m_cameraNext;           // what the scenes' update set, taken by the next update()
    bool m_cameraEnded = false;                        // the scenes' update ended the scene camera
    bool m_preloadPending = false;                     // the scene camera has just started
    FrontEndObjectSource m_source;
    std::unique_ptr<world_objects::ObjectList> m_objectList; // before the objects that read it
    std::unique_ptr<PlacedObjects> m_objects;
    std::map<double, scenes::ScenePose> m_poses;                // the newest pose a scene gave each object
    std::map<double, Interpolated<scenes::ScenePose>> m_placed; // each drawn object's pose at the last two steps
    std::set<double> m_hidden;                                  // the objects a message 0x13 hid
    std::uint64_t m_sceneLights = 0;
};

/// The view through a scene camera at `pose` (the game's axes; the camera looks along its rotation's +y with +z up,
/// docs/research/scenes.md#coneys-implementation) with `lens`, in RenderWare's axes, with the lens's view window on
/// the 4:3 picture.
[[nodiscard]] WorldView sceneCameraWorldView(const scenes::ScenePose& pose, const scenes::SceneLens& lens);

} // namespace coney::platform
