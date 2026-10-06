// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>

#include "core/error.h"
#include "fileio/wad.h"
#include "gamemodes/front_end_scene.h"
#include "platform/render_engine.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "world/level_object.h"
#include "world/sector_budget.h"

namespace coney::platform {

/// The front end's 3D background (docs/research/frontend.md#background): `level100`'s streamed worlds and level file,
/// streamed one decision a step around the camera and drawn by the world renderer, cleared to black (the background
/// colour `LevelFlow_StartFrontEnd` sets), with the menus' 2D pass and fade drawn over them before the frame is shown.
///
/// **Coney's stand-in camera** until the scene player (src/scenes, being written) plays `WonderWheel_100`, whose
/// `camera01` track is the real view: the track's first pose, (462.60, −122.35, −187.93) in the game's axes, with its
/// lens (field of view 54.43° across, near 0.5, far 150), turned to the wheel's hub at (515.51, −68.89, −188.67) and
/// then kHubYawOffsetDegrees to the left, so the hub sits right of centre where the runtime picture has the wheel (its
/// outline across logical x 335-615 of 640). The camera does not move, as at runtime. The scene's objects (the wheel,
/// its carts and neon signs, spawned by `level100.lua`'s `ObjSpawn`) are not drawn: Coney cannot load a dynamic
/// object's model yet.
///
/// Lighting is the world renderer's stand-in ambient (no LightManager yet).
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
           const std::function<void(std::string_view)>& print);

    /// One streaming decision round the camera and the visibility pass.
    void update(std::uint64_t nowMs) override;

    /// The world through the camera, then `overlay`, then the present.
    void render(const RenderTime& time, const std::function<void()>& overlay) override;

    /// The view the scene is drawn through.
    [[nodiscard]] const WorldView& view() const { return m_view; }

  private:
    explicit FrontEndWorldScene(RenderEngine& engine);

    RenderEngine& m_engine;
    world::SectorBudget m_budget{world::kSectorPoolSize}; // before the worlds charged to it
    std::unique_ptr<WorldSet> m_set;
    std::unique_ptr<world::LevelObject> m_level;
    WorldRenderer m_renderer;
    WorldView m_view;
    float m_pendingDistance = 0.0F;
};

/// The stand-in camera's view: kCameraX/Y/Z turned to the hub and kHubYawOffsetDegrees left, in RenderWare's axes,
/// with the lens's view window on the 4:3 picture.
[[nodiscard]] WorldView frontEndSceneView();

} // namespace coney::platform
