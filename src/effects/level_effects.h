// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "effects/blur_pulse.h"
#include "effects/camera_litter.h"
#include "effects/ground_fog.h"
#include "effects/motion_blur.h"
#include "effects/particles.h"
#include "effects/room_smoke.h"
#include "effects/screen_tint.h"

namespace coney::effects {

/// A level's effects that scripts and the engine start: the particle systems, player 1's view's motion blur, screen
/// tint and blur pulse, ground fog and room smoke, and the litter round the camera. Gameplay owns one for each level,
/// steps it after the level's step and hands it to the level to draw.
struct LevelEffects {
    ParticleSystems particles;
    MotionBlur motionBlur;
    /// The screen tint: SetLevelColour's and the stores' looks, and the blends the cameras ask for (the game-over
    /// shot and CamUseDeathCamera: `tint.blendTintTo(ScreenTint::kGameOver, seconds)`).
    ScreenTint tint;
    /// The blur pulse: `ScreenQueueEffect` 4 and 5, and the cameras' (`blurPulse.start(seconds, delayMs)`,
    /// `blurPulse.end(0)`).
    BlurPulse blurPulse;
    GroundFog fog;
    CameraLitter litter;
    RoomSmoke smoke;
    /// The level's collision for the litter's rays (empty: the litter meets nothing).
    LitterRay litterRay;

    /// One fixed step of `seconds`, seen from `viewer` (none before the level has a camera: the steam vents and the
    /// fog and the litter wait).
    void step(float seconds, const std::optional<EffectsViewer>& viewer = std::nullopt) {
        particles.setViewer(viewer ? std::optional<anim::Vec3>(viewer->position) : std::nullopt);
        // The fly piles' view test: the view's frustum widened by the margin (no window: every point is in view).
        if (viewer) {
            particles.setViewTest([eye = viewer->position, window = viewer->window](anim::Vec3 point, float margin) {
                return !window || nearView(eye, *window, point, margin);
            });
        } else {
            particles.setViewTest({});
        }
        particles.step(seconds);
        motionBlur.step(seconds);
        tint.step(seconds);
        blurPulse.step(seconds);
        if (viewer) {
            fog.step(seconds, *viewer);
            litter.step(seconds, viewer->position, litterRay);
        }
        smoke.step(seconds, viewer);
    }
};

} // namespace coney::effects
