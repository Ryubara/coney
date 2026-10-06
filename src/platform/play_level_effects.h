// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "effects/level_effects.h"
#include "fileio/wad.h"
#include "platform/motion_blur_pass.h"
#include "platform/particle_renderer.h"
#include "platform/render_engine.h"
#include "world/view_frustum.h"

namespace coney::platform {

/// What the play mode draws of a level's scripted world beyond its humans: the particle systems among the scenery and
/// the motion blur over the 3D frame (effects::LevelEffects). Gameplay steps them; this only draws.
class PlayLevelEffects {
  public:
    /// Draws `effects` (which must outlive this), loading sprite sheets from `wad`; `print` gets what fails to load.
    /// Needs a running RenderEngine; must be destroyed before it stops.
    PlayLevelEffects(RenderEngine& engine, const io::Wad& wad, const effects::LevelEffects& effects,
                     std::function<void(std::string_view)> print);

    /// Draws the particle systems through the current camera at `view` (RenderWare's axes), after everything solid.
    void drawInScene(const world::CameraPose& view);
    /// Lays the motion blur over the 3D frame, before the 2D overlays.
    void drawOverlay(RenderEngine& engine);

  private:
    RenderEngine& m_engine;
    const effects::LevelEffects& m_effects;
    ParticleRenderer m_particles;
    MotionBlurPass m_motionBlur;
};

} // namespace coney::platform
