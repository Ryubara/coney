// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "effects/motion_blur.h"
#include "effects/particles.h"

namespace coney::effects {

/// A level's effects that scripts and the engine start: the particle systems and player 1's view's motion blur.
/// Gameplay owns one for each level, steps it after the level's step and hands it to the level to draw.
struct LevelEffects {
    ParticleSystems particles;
    MotionBlur motionBlur;

    /// One fixed step of `seconds`.
    void step(float seconds) {
        particles.step(seconds);
        motionBlur.step(seconds);
    }
};

} // namespace coney::effects
