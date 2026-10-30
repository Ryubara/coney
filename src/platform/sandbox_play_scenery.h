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

#include "core/error.h"
#include "fileio/wad.h"
#include "platform/play_level_mode.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/sandbox_renderer.h"
#include "sandbox/sandbox_ground.h"
#include "sandbox/sandbox_layout.h"
#include "sandbox/sandbox_world.h"

namespace coney::platform {

/// A sandbox for the play mode (docs/guides/sandbox.md): its ground (sandbox::SandboxGround: the collision mesh to
/// stand on, the player starting at one of the layout's spawn points), drawn by the sandbox renderer. Nothing streams;
/// the draw distance is the layout's fog end. Coney's own test worlds, in the debug tools' target
/// (docs/guides/building.md#debug-tools).
class SandboxPlayScenery final : public PlayScenery {
  public:
    /// The scenery of `world`, the player starting at spawn point `spawn` (the layout's first when unset). Fails as
    /// sandbox::SandboxGround::create() and SandboxRenderer::create() do.
    [[nodiscard]] static std::expected<std::unique_ptr<SandboxPlayScenery>, Error>
    create(const RenderEngine& engine, sandbox::SandboxWorld world, const std::optional<std::string>& spawn);

    [[nodiscard]] const raycast::CollisionMesh& collision() const override { return m_ground->collision(); }
    [[nodiscard]] human::PlayerStart start() const override { return m_ground->start(); }
    [[nodiscard]] std::string startSource() const override { return m_ground->startSource(); }
    void preload(world::Vec3 /*camera*/) override {}
    void step(world::Vec3 /*camera*/, const FrameTime& /*frame*/) override {}
    [[nodiscard]] float drawDistance() const override { return world().layout().lighting.fogEnd; }
    void findVisible(const WorldView& /*newest*/) override {}
    void draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
              const std::function<void()>& drawObjects, const std::function<void()>& afterWorlds) override;
    /// The lights the humans and objects are drawn with; each frame starts their viewport pass before drawing.
    void setLighting(SceneLighting* lighting) override { m_lighting = lighting; }
    [[nodiscard]] anim::Vec3 lightDirection() const override { return world().layout().lighting.sunDirection; }
    [[nodiscard]] std::string summary() const override;

    /// The sandbox's world, as its ground holds it.
    [[nodiscard]] const sandbox::SandboxWorld& world() const { return m_ground->world(); }

    [[nodiscard]] std::string name() const override { return m_ground->name(); }
    [[nodiscard]] std::vector<human::Place> places() const override { return m_ground->places(); }
    [[nodiscard]] bool canSpawn() const override { return true; }
    [[nodiscard]] std::vector<world::SceneTarget> targets() const override { return m_ground->targets(); }
    [[nodiscard]] std::vector<human::Place> fighters() const override { return m_ground->fighters(); }
    /// Rebuilds the sandbox with the primitives `extras` was made from (sandboxPrimitivesOf()) added to the layout as
    /// made; none: as made. Fails as the rebuild does, and for a layout left with nothing solid; the sandbox is then
    /// unchanged.
    std::expected<void, Error> setExtras(const RenderEngine& engine, SceneExtras extras) override;
    [[nodiscard]] const SceneExtras& extras() const override { return m_extras; }

  private:
    SandboxPlayScenery(std::unique_ptr<sandbox::SandboxGround> ground, std::unique_ptr<SandboxRenderer> renderer);

    std::unique_ptr<sandbox::SandboxGround> m_ground; // the world, its collision and the start
    sandbox::SandboxLayout m_made; // the layout as made, before any extras: the base setExtras() builds on
    std::unique_ptr<SandboxRenderer> m_renderer;
    SceneLighting* m_lighting = nullptr; // the play mode's lights, started each frame for the humans and objects
    SceneExtras m_extras;                // the added primitives' count and source (setExtras())
};

/// The play mode in the sandbox `world` (SandboxPlayScenery::create()), at spawn point `spawn` (the layout's first when
/// unset), with the character loaded from `wad`, as `setup` says (its start is always snapped): `--play-level
/// sandbox:NAME` and the Levels page's layouts (docs/guides/sandbox.md). Fails as the scenery and the character loader
/// do.
[[nodiscard]] std::expected<std::unique_ptr<PlayLevelMode>, Error>
createPlayInSandbox(RenderEngine& engine, const io::Wad& wad, sandbox::SandboxWorld world,
                    const std::optional<std::string>& spawn, std::function<void(std::string_view)> print,
                    const PlayerSetup& setup = {});

/// The objects `primitives` make, as a scenery takes them (PlayScenery::setExtras()): built as a sandbox builds them
/// (the drawn mesh with its baked light and tints, and the solid triangles, sandbox::sandboxCollisionTriangles()),
/// drawn by a SandboxRenderer of their own, with `primitives` kept as their source. None: empty extras. Fails as
/// building them or their renderer does.
[[nodiscard]] std::expected<SceneExtras, Error> sandboxExtras(const RenderEngine& engine,
                                                              std::vector<sandbox::Primitive> primitives);

/// The primitives `extras` was made from (sandboxExtras()); empty for none.
[[nodiscard]] std::vector<sandbox::Primitive> sandboxPrimitivesOf(const SceneExtras& extras);

} // namespace coney::platform
