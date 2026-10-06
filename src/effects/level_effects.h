// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "effects/ground_fog.h"
#include "effects/motion_blur.h"
#include "effects/particles.h"

namespace coney::effects {

/// A level's effects that scripts and the engine start: the particle systems, player 1's view's motion blur and ground
/// fog, and the litter round the camera. Gameplay owns one for each level, steps it after the level's step and hands it
/// to the level to draw.
struct LevelEffects {
    ParticleSystems particles;
    MotionBlur motionBlur;
    GroundFog fog;
    CameraLitter litter;

    /// One fixed step of `seconds`, seen from `viewer` (none before the level has a camera: the steam vents and the
    /// fog wait).
    void step(float seconds, const std::optional<EffectsViewer>& viewer = std::nullopt) {
        particles.setViewer(viewer ? std::optional<anim::Vec3>(viewer->position) : std::nullopt);
        particles.step(seconds);
        motionBlur.step(seconds);
        if (viewer) {
            fog.step(seconds, *viewer);
        }
    }
};

} // namespace coney::effects
