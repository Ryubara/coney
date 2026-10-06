// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "human/player.h"
#include "platform/render_engine.h"
#include "platform/sandbox_renderer.h"
#include "platform/world_renderer.h"
#include "platform/world_viewer_mode.h"
#include "raycast/collision_mesh.h"
#include "sandbox/sandbox_world.h"
#include "world/sector_budget.h"

namespace coney::platform {

/// Where the play mode's player stands and what is drawn round him: a level's streamed scenery or a sandbox. The play
/// mode (PlayLevelMode) owns the player, the follow camera and the character; the scenery owns the ground they collide
/// with and draws everything else, so the same player and camera code runs in a level and in a sandbox.
///
/// The mode calls step() and findVisible() in its update() (one fixed step) and draw() in its render() (a blended
/// frame); none of them reads a clock (docs/guides/conventions.md#update-and-render).
class PlayScenery {
  public:
    virtual ~PlayScenery() = default;
    PlayScenery() = default;
    PlayScenery(const PlayScenery&) = delete;
    PlayScenery& operator=(const PlayScenery&) = delete;
    PlayScenery(PlayScenery&&) = delete;
    PlayScenery& operator=(PlayScenery&&) = delete;

    /// The collision mesh the player stands on and the camera tests against. Valid as long as the scenery.
    [[nodiscard]] virtual const raycast::CollisionMesh& collision() const = 0;
    /// Where the player starts.
    [[nodiscard]] virtual human::PlayerStart start() const = 0;
    /// Where start() comes from, for the player's line ("the level's start", "spawn point lane").
    [[nodiscard]] virtual std::string startSource() const = 0;
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
    /// `drawObjects` draws the characters among the scenery. `nowMs` is the frame's game time. Changes nothing the
    /// simulation reads.
    virtual void draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
                      const std::function<void()>& drawObjects) = 0;
    /// The direction the character's directional light travels, in the game's axes.
    [[nodiscard]] virtual anim::Vec3 lightDirection() const = 0;
    /// The scenery's counts for the play summary, starting with "; ".
    [[nodiscard]] virtual std::string summary() const = 0;

    /// What is played, for the debug menus: the level's name, or "sandbox " and the layout's title.
    [[nodiscard]] virtual std::string name() const = 0;
    /// The named places the debug menus' teleport offers: the start (a level), or the spawn points (a sandbox).
    [[nodiscard]] virtual std::vector<debug::Place> places() const;
    /// The passive humans to fight (Coney's own sandbox targets): a sandbox layout's `target` lines; none in a level.
    [[nodiscard]] virtual std::vector<sandbox::TargetPoint> targets() const { return {}; }
    /// The AI humans that fight the player: a sandbox layout's `fighter` lines; none in a level.
    [[nodiscard]] virtual std::vector<sandbox::FighterPoint> fighters() const { return {}; }
    /// Whether the debug menus' Spawner can add objects (a sandbox can).
    [[nodiscard]] virtual bool canSpawn() const { return false; }
    /// Rebuilds the scenery with `extra` objects added to what it was made with (none: as made). Fails with
    /// ErrorCode::InvalidArgument where nothing can be added (a level), and as the rebuild does; the scenery is then
    /// unchanged.
    virtual std::expected<void, Error> setExtras(const RenderEngine& engine,
                                                 const std::vector<sandbox::Primitive>& extra);
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
              const std::function<void()>& drawObjects) override;
    [[nodiscard]] anim::Vec3 lightDirection() const override;
    [[nodiscard]] std::string summary() const override;
    [[nodiscard]] std::string name() const override { return m_name; }

  private:
    LevelPlayScenery(LevelScenery scenery, world::SectorBudget& budget, const human::PlayerStart& start,
                     std::string startSource, std::function<void(std::string_view)> print);

    LevelScenery m_scenery;
    world::SectorBudget& m_budget;
    human::PlayerStart m_start;
    std::string m_startSource; // where m_start comes from, for the player's line
    WorldRenderer m_renderer;
    std::function<void(std::string_view)> m_print;
    std::string m_name; // the level's name
    float m_drawDistance;
    float m_pending = 0.0F; // the nearest missing scenery after the last step
    std::uint32_t m_loads = 0;
    std::uint32_t m_unloads = 0;
    std::uint32_t m_failures = 0;
};

/// A sandbox for the play mode (docs/guides/sandbox.md): its collision mesh to stand on, drawn by the sandbox
/// renderer, the player starting at one of the layout's spawn points. Nothing streams; the draw distance is the
/// layout's fog end.
class SandboxPlayScenery final : public PlayScenery {
  public:
    /// The scenery of `world`, the player starting at spawn point `spawn` (the layout's first when unset). Fails with
    /// ErrorCode::NotFound for an unknown spawn and for a layout with nothing solid, and as SandboxRenderer::create().
    [[nodiscard]] static std::expected<std::unique_ptr<SandboxPlayScenery>, Error>
    create(const RenderEngine& engine, sandbox::SandboxWorld world, const std::optional<std::string>& spawn);

    [[nodiscard]] const raycast::CollisionMesh& collision() const override { return *m_world.collision(); }
    [[nodiscard]] human::PlayerStart start() const override { return m_start; }
    [[nodiscard]] std::string startSource() const override { return "spawn point " + m_spawn; }
    void preload(world::Vec3 /*camera*/) override {}
    void step(world::Vec3 /*camera*/, const FrameTime& /*frame*/) override {}
    [[nodiscard]] float drawDistance() const override { return m_world.layout().lighting.fogEnd; }
    void findVisible(const WorldView& /*newest*/) override {}
    void draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
              const std::function<void()>& drawObjects) override;
    [[nodiscard]] anim::Vec3 lightDirection() const override { return m_world.layout().lighting.sunDirection; }
    [[nodiscard]] std::string summary() const override;

    [[nodiscard]] const sandbox::SandboxWorld& world() const { return m_world; }

    [[nodiscard]] std::string name() const override { return "sandbox " + m_world.layout().title; }
    [[nodiscard]] std::vector<debug::Place> places() const override;
    [[nodiscard]] bool canSpawn() const override { return true; }
    [[nodiscard]] std::vector<sandbox::TargetPoint> targets() const override { return m_world.layout().targets; }
    [[nodiscard]] std::vector<sandbox::FighterPoint> fighters() const override { return m_world.layout().fighters; }
    std::expected<void, Error> setExtras(const RenderEngine& engine,
                                         const std::vector<sandbox::Primitive>& extra) override;

  private:
    SandboxPlayScenery(sandbox::SandboxWorld world, std::unique_ptr<SandboxRenderer> renderer,
                       const human::PlayerStart& start, std::string spawn);

    sandbox::SandboxWorld m_world;
    sandbox::SandboxLayout m_made; // the layout as made, before any extras: the base setExtras() builds on
    std::unique_ptr<SandboxRenderer> m_renderer;
    human::PlayerStart m_start;
    std::string m_spawn; // the spawn point's name
};

/// **Coney's choice** for a level whose player start is not researched: above the middle of the first world's part 1
/// (the world viewer's start), dropped onto the collision mesh from 200 m above its lowest point. Game axes.
[[nodiscard]] human::PlayerStart playLevelStandInStart(const WorldSet& set, const raycast::CollisionMesh& mesh);

/// A point in the game's axes (z up) in RenderWare's (y up), as the world uses them: (x, z, -y).
[[nodiscard]] world::Vec3 toRenderWare(anim::Vec3 game);

/// A direction in the game's axes in RenderWare's, as toRenderWare() turns points.
[[nodiscard]] anim::Vec3 directionToRenderWare(anim::Vec3 game);

} // namespace coney::platform
