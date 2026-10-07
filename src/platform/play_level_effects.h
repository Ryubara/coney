// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "effects/level_effects.h"
#include "fileio/wad.h"
#include "platform/motion_blur_pass.h"
#include "platform/parked_cars.h"
#include "platform/particle_renderer.h"
#include "platform/render_engine.h"
#include "platform/room_smoke_overlay.h"
#include "raycast/collision_builder.h"
#include "world/view_frustum.h"
#include "world_objects/cars.h"

namespace coney::platform {

/// What the play mode draws of a level's scripted world beyond its humans and objects: the parked cars
/// (world_objects::Cars) and the particle systems among the scenery, and the motion blur over the 3D frame
/// (effects::LevelEffects). Gameplay steps them; this only draws them, and gives the cars' boxes to the collision.
class PlayLevelEffects {
  public:
    /// Draws `effects` and `cars` (either may be null; both must outlive this), loading sprite sheets and car models
    /// from `wad`; `print` gets what fails to load. Needs a running RenderEngine started with the world plugins; must
    /// be destroyed before it stops.
    PlayLevelEffects(RenderEngine& engine, const io::Wad& wad, const effects::LevelEffects* effects,
                     const world_objects::Cars* cars, std::function<void(std::string_view)> print);

    /// The parked cars' boxes for the level's collision (none without cars).
    [[nodiscard]] std::vector<raycast::BuildTriangle> carObstacles();
    /// Draws the parked cars with the solid objects, through the current camera, each atomic lit and drawn by
    /// `render`.
    void drawCars(const std::function<void(rw::Atomic*)>& render);
    /// Draws the particle systems through the current camera at `view` (RenderWare's axes), after everything solid.
    void drawInScene(const world::CameraPose& view);
    /// Lays the motion blur, then the room smoke, over the 3D frame, before the 2D overlays.
    void drawOverlay(RenderEngine& engine);

  private:
    RenderEngine& m_engine;
    const effects::LevelEffects* m_effects;
    ParticleRenderer m_particles;
    MotionBlurPass m_motionBlur;
    RoomSmokeOverlay m_smoke;
    std::unique_ptr<ParkedCars> m_cars; // null without cars
};

} // namespace coney::platform
