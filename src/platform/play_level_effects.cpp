// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_level_effects.h"

#include <utility>

namespace coney::platform {

PlayLevelEffects::PlayLevelEffects(RenderEngine& engine, const io::Wad& wad, const effects::LevelEffects* effects,
                                   const world_objects::Cars* cars, std::function<void(std::string_view)> print)
    : m_engine(engine), m_effects(effects), m_particles(wad, engine.drawsPixels(), print), m_smoke(wad, print) {
    if (cars != nullptr) {
        m_cars = std::make_unique<ParkedCars>(wad, *cars, engine.drawsPixels(), std::move(print));
    }
}

std::vector<raycast::BuildTriangle> PlayLevelEffects::carObstacles() {
    return m_cars ? m_cars->obstacles() : std::vector<raycast::BuildTriangle>{};
}

void PlayLevelEffects::drawCars(const std::function<void(rw::Atomic*)>& render, graphics::CarPass pass) {
    if (m_cars && m_engine.drawsPixels()) {
        m_cars->draw(render, pass);
    }
}

void PlayLevelEffects::drawInScene(const world::CameraPose& view) {
    // The NULL backend draws nothing, so the sheets are never loaded there.
    if (m_effects != nullptr && m_engine.drawsPixels()) {
        m_particles.draw(m_effects->particles, view);
    }
}

void PlayLevelEffects::drawOverlay(RenderEngine& engine) {
    if (m_effects != nullptr) {
        m_motionBlur.apply(engine, m_effects->motionBlur.current());
        m_smoke.draw(engine, m_effects->smoke);
    }
}

} // namespace coney::platform
