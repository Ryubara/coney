// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "effects/level_effects.h"
#include "fileio/wad.h"
#include "platform/motion_blur_pass.h"
#include "platform/parked_cars.h"
#include "platform/particle_renderer.h"
#include "platform/render_engine.h"
#include "platform/room_smoke_overlay.h"
#include "platform/water_renderer.h"
#include "world/view_frustum.h"
#include "world_objects/cars.h"

namespace coney::platform {

/// What the play mode draws of a level's scripted world beyond its humans and objects: the parked cars
/// (world_objects::Cars) and the particle systems among the scenery, and the motion blur over the 3D frame
/// (effects::LevelEffects). Gameplay steps them; this only draws them. A walker meets a parked car through the level's
/// own collision mesh, its shell there (docs/research/characters.md#car-shells), never through the car.
class PlayLevelEffects {
  public:
    /// Draws `effects` and `cars` (either may be null; both must outlive this), loading sprite sheets and car models
    /// from `wad`; `print` gets what fails to load. Needs a running RenderEngine started with the world plugins; must
    /// be destroyed before it stops.
    PlayLevelEffects(RenderEngine& engine, const io::Wad& wad, const effects::LevelEffects* effects,
                     const world_objects::Cars* cars, std::function<void(std::string_view)> print);

    /// Draws the parked cars' `pass` (docs/research/graphics.md#car-draw): the opaque parts with the solid objects, the
    /// glass after the see-through ones; through the current camera, each atomic lit and drawn by `render`.
    void drawCars(const std::function<void(rw::Atomic*)>& render, graphics::CarPass pass);
    /// Draws the particle systems through the current camera at `view` (RenderWare's axes), after everything solid.
    void drawInScene(const world::CameraPose& view);
    /// Draws the glints of the pickups and lock-pickable doors (effects::Triglints::sprites()) through the current
    /// camera at `view`, blended by their alpha, after the particle systems.
    void drawGlints(std::span<const effects::Particle> glints, const world::CameraPose& view);
    /// Draws the level's water (effects::Water) through the current camera, after the `d` world.
    void drawWater();
    /// Lays the motion blur, then the room smoke, over the 3D frame, before the 2D overlays. The smoke is left out
    /// while `cardHeld`: a scene holds an intro card (`0x00512c44`, docs/research/graphics.md#room-smoke).
    void drawOverlay(RenderEngine& engine, bool cardHeld);
    /// Lays the screen tint (effects::ScreenTint) over the whole screen: after the HUD, before the scene's captions
    /// (docs/research/rendering.md#tint).
    void drawTint(RenderEngine& engine);
    /// The view's screen effects in the original's order (docs/research/graphics.md#blur-pulse): the blur pulse, which
    /// replaces the screen with its blurred copy, then the tint.
    /// @orig 0x0018dac0 ScreenFx_Render (ScreenEffectsManager.cpp)
    void drawScreenEffects(RenderEngine& engine);
    /// Whether the screen effects go before the HUD this frame: while a blur pulse runs.
    [[nodiscard]] bool screenEffectsFirst() const;

  private:
    // Draws the ground fog's wisps (effects::GroundFog::drawn()) through the current camera at `view`.
    void drawFog(const world::CameraPose& view);

    RenderEngine& m_engine;
    const effects::LevelEffects* m_effects;
    ParticleRenderer m_particles;
    MotionBlurPass m_motionBlur;
    RoomSmokeOverlay m_smoke;
    WaterRenderer m_water;
    std::unique_ptr<ParkedCars> m_cars; // null without cars
};

} // namespace coney::platform
