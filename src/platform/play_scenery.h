// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "human/place.h"
#include "human/player.h"
#include "platform/render_engine.h"
#include "platform/world_edit_hook.h"
#include "platform/world_renderer.h"
#include "platform/world_viewer_mode.h"
#include "raycast/collision_builder.h"
#include "raycast/collision_mesh.h"
#include "world/level_ground.h"
#include "world/path_map.h"
#include "world/sector_budget.h"

namespace coney::platform {

/// Objects added to a scenery after it was made: Coney's own, the debug tools' Spawner (PlayLevelControls::spawn(),
/// docs/guides/debug-menu.md#pages). Their solid triangles join the collision mesh after the scenery's own, `draw`
/// draws them among the scenery's objects in its camera, states and fog, and `source` is what they were made from,
/// kept for whoever made them. Empty: none.
struct SceneExtras {
    std::vector<raycast::BuildTriangle> solid;
    std::function<void(RenderEngine&)> draw;
    std::size_t count = 0; ///< How many objects.
    std::shared_ptr<const void> source;
};

/// Where the play mode's player stands and what is drawn round him: a level's streamed scenery or a sandbox. The play
/// mode (PlayLevelMode) owns the player, the follow camera and the character; the scenery owns the ground they collide
/// with and draws everything else, so the same player and camera code runs in a level and in a sandbox. What the
/// simulation reads of it (the collision, the start, the places, the path data) is its world::LevelGround side; this
/// class adds the streaming round the camera and the drawing.
///
/// The mode calls step() and findVisible() in its update() (one fixed step) and draw() in its render() (a blended
/// frame); none of them reads a clock (docs/guides/conventions.md#update-and-render).
class PlayScenery : public world::LevelGround {
  public:
    /// Before the first frame, with the camera at `camera` (RenderWare's axes).
    virtual void preload(world::Vec3 camera) = 0;
    /// The scenery's part of a simulation step, with the camera at `camera`: streaming, the draw distance.
    virtual void step(world::Vec3 camera, const FrameTime& frame) = 0;
    /// The far clip after the last step.
    [[nodiscard]] virtual float drawDistance() const = 0;
    /// The end of a step: what the scenery decides from the newest step's view (RenderWare's axes) that the next step
    /// reads, such as a level's visible sectors.
    virtual void findVisible(const WorldView& newest) = 0;
    /// Draws one frame through `view` (RenderWare's axes, blended between the last two steps) and presents it;
    /// `drawObjects` draws the characters among the scenery, `afterWorlds` what follows the level's last world (the
    /// water and the see-through parts). `nowMs` is the frame's game time. Changes nothing the simulation reads.
    virtual void draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
                      const std::function<void()>& drawObjects, const std::function<void()>& afterWorlds) = 0;
    /// Holds the scenery's own animations (clouds, light flicker) at game time `atMs` from now on, whatever draw() is
    /// given, while what streams in still fades in: `--freeze-world`. A scenery without animations ignores it.
    virtual void freezeAnimation(std::uint64_t atMs) { (void)atMs; }
    /// The direction the character's directional light travels, in the game's axes: the stand-in lighting of a scenery
    /// without a level's lights.
    [[nodiscard]] virtual anim::Vec3 lightDirection() const = 0;
    /// Draws with `lighting` (the play mode's, which outlives the scenery's drawing) from now on; a scenery that lights
    /// itself ignores it.
    virtual void setLighting(SceneLighting* /*lighting*/) {}
    /// The scenery's counts for the play summary, starting with "; ".
    [[nodiscard]] virtual std::string summary() const = 0;
    /// Whether objects can be added (setExtras(): a sandbox and a level can).
    [[nodiscard]] virtual bool canSpawn() const { return false; }
    /// Puts `extras` in the scenery in place of those added before (empty: as made). Fails with
    /// ErrorCode::InvalidArgument where nothing can be added, and as adding them does; the scenery is then unchanged.
    virtual std::expected<void, Error> setExtras(const RenderEngine& engine, SceneExtras extras);
    /// The objects added now (setExtras()).
    [[nodiscard]] virtual const SceneExtras& extras() const;
    /// Changes the scenery through the debug tools' world edits from now on (WorldEditHook), called after each step's
    /// streaming and drawn through; null: none. A scenery without streamed worlds (a sandbox) ignores it.
    /// @coney-only the debug tools' world edits; the original has none
    virtual void setEditHook(const std::shared_ptr<WorldEditHook>& /*hook*/) {}
};

/// A level's scenery for the play mode: its streamed worlds and level file (loadLevelScenery()), streamed around the
/// camera one decision a step, the draw distance following the nearest missing scenery, the visible sectors found at
/// the end of each step, drawn by the world renderer. The player starts where the level script created him (the start
/// given to load()), else at the level's researched start, else at Coney's stand-in (playLevelStandInStart()).
class LevelPlayScenery final : public PlayScenery {
  public:
    /// Loads level `name` from `wad`, charging `budget` (which must outlive the scenery). Fails as loadLevelScenery()
    /// does, and with ErrorCode::NotFound for a level without a level file (no collision to stand on). The player
    /// starts at `scriptStart`, player 1 as the level script created him, when given.
    [[nodiscard]] static std::expected<std::unique_ptr<LevelPlayScenery>, Error>
    load(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
         std::function<void(std::string_view)> print, std::optional<human::PlayerStart> scriptStart = std::nullopt);

    [[nodiscard]] const raycast::CollisionMesh& collision() const override { return *m_scenery.level->collision; }
    [[nodiscard]] human::PlayerStart start() const override { return m_start; }
    [[nodiscard]] std::string startSource() const override { return m_startSource; }
    void preload(world::Vec3 camera) override;
    void step(world::Vec3 camera, const FrameTime& frame) override;
    [[nodiscard]] float drawDistance() const override { return m_drawDistance; }
    void findVisible(const WorldView& newest) override;
    void draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
              const std::function<void()>& drawObjects, const std::function<void()>& afterWorlds) override;
    [[nodiscard]] anim::Vec3 lightDirection() const override;
    void setLighting(SceneLighting* lighting) override { m_renderer.setLighting(lighting); }
    void freezeAnimation(std::uint64_t atMs) override { m_renderer.freezeAnimation(atMs); }
    [[nodiscard]] std::string summary() const override;
    [[nodiscard]] std::string name() const override { return m_name; }
    [[nodiscard]] const world::PathMap* pathMap() const override { return m_paths ? &*m_paths : nullptr; }
    [[nodiscard]] raycast::CollisionMesh* objectCollision() override { return m_scenery.level->collision.get(); }
    [[nodiscard]] world::PathMap* objectPaths() override { return m_paths ? &*m_paths : nullptr; }
    /// A level can take the Spawner's objects: they join its collision mesh and are drawn among its scenery.
    [[nodiscard]] bool canSpawn() const override { return true; }
    /// Puts `extras` in the level in place of the objects added before: their solid triangles are added to the
    /// level's collision mesh after its own (raycast::CollisionMesh::addExtraTriangles()), so the player, the AI, the
    /// climbs and the cameras meet them through the queries they use for the level, and they are drawn first among
    /// the world's objects. None leaves the mesh as the disc has it. Fails as adding the triangles does; the level is
    /// then unchanged.
    std::expected<void, Error> setExtras(const RenderEngine& engine, SceneExtras extras) override;
    [[nodiscard]] const SceneExtras& extras() const override { return m_extras; }
    /// The world edits run after each step's streaming, on the level's worlds and collision mesh, and the renderer
    /// draws through them. They add their collision as the mesh's extra triangles, so they do not mix with the
    /// Spawner's objects (setExtras()).
    void setEditHook(const std::shared_ptr<WorldEditHook>& hook) override;

  private:
    LevelPlayScenery(LevelScenery scenery, world::SectorBudget& budget, const human::PlayerStart& start,
                     std::string startSource, std::function<void(std::string_view)> print);

    LevelScenery m_scenery;
    world::SectorBudget& m_budget;
    human::PlayerStart m_start;
    std::string m_startSource; // where m_start comes from, for the player's line
    WorldRenderer m_renderer;
    std::function<void(std::string_view)> m_print;
    std::string m_name;                    // the level's name
    std::optional<world::PathMap> m_paths; // the level file's path data, decoded
    float m_drawDistance;
    float m_pending = 0.0F; // the nearest missing scenery after the last step
    std::uint32_t m_loads = 0;
    std::uint32_t m_unloads = 0;
    std::uint32_t m_failures = 0;
    SceneExtras m_extras;                   // the added objects (setExtras()), their triangles in the collision mesh
    std::shared_ptr<WorldEditHook> m_edits; // setEditHook(); null: none
    std::uint64_t m_editSteps = 0;          // steps the world edits have seen
};

/// **Coney's choice** for a level whose player start is not researched: above the middle of the first world's part 1
/// (the world viewer's start), dropped onto the collision mesh from 200 m above its lowest point. Game axes.
[[nodiscard]] human::PlayerStart playLevelStandInStart(const WorldSet& set, const raycast::CollisionMesh& mesh);

/// A point in the game's axes (z up) in RenderWare's (y up), as the world uses them: (x, z, -y).
[[nodiscard]] world::Vec3 toRenderWare(anim::Vec3 game);

/// A direction in the game's axes in RenderWare's, as toRenderWare() turns points.
[[nodiscard]] anim::Vec3 directionToRenderWare(anim::Vec3 game);

/// The camera `view` (game axes: its orientation's +y the view direction, +z up) as the world renderer takes it, in
/// RenderWare's axes, its 4:3 view window drawn at `aspect` (width over height, RenderEngine::viewAspect()) keeping its
/// height, and `drawDistance` as the far clip. Right is forward x up, as the play camera's.
[[nodiscard]] WorldView worldViewOf(const camera::CameraView& view, float aspect, float drawDistance);

} // namespace coney::platform
