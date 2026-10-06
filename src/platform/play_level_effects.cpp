// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_level_effects.h"

#include <utility>

namespace coney::platform {

PlayLevelEffects::PlayLevelEffects(RenderEngine& engine, const io::Wad& wad, const effects::LevelEffects& effects,
                                   std::function<void(std::string_view)> print)
    : m_engine(engine), m_effects(effects), m_particles(wad, engine.drawsPixels(), std::move(print)) {}

void PlayLevelEffects::drawInScene(const world::CameraPose& view) {
    // The NULL backend draws nothing, so the sheets are never loaded there.
    if (m_engine.drawsPixels()) {
        m_particles.draw(m_effects.particles, view);
    }
}

void PlayLevelEffects::drawOverlay(RenderEngine& engine) { m_motionBlur.apply(engine, m_effects.motionBlur.current()); }

} // namespace coney::platform
