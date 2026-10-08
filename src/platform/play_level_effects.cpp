// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_level_effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "effects/blur_pulse.h"
#include "effects/glints.h"
#include "effects/ground_fog.h"
#include "effects/screen_tint.h"
#include "graphics/render_device.h"
#include "graphics/screen_fade.h"

namespace coney::platform {

PlayLevelEffects::PlayLevelEffects(RenderEngine& engine, const io::Wad& wad, const effects::LevelEffects* effects,
                                   const world_objects::Cars* cars, std::function<void(std::string_view)> print)
    : m_engine(engine), m_effects(effects), m_particles(wad, engine.drawsPixels(), print), m_smoke(wad, print),
      m_water(wad, print) {
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
        drawFog(view);
    }
}

void PlayLevelEffects::drawFog(const world::CameraPose& view) {
    const effects::GroundFog& fog = m_effects->fog;
    if (!fog.settings()) {
        return;
    }
    // Camera-facing squares of rectangle 0 (the whole sheet), blended by their alpha with the other 3D sprites.
    std::vector<effects::Particle> sprites;
    for (const effects::GroundFog::Drawn& wisp : fog.drawn()) {
        effects::Particle sprite;
        sprite.position = wisp.position;
        sprite.size = wisp.size;
        sprite.colour = wisp.colour;
        sprite.rect = 0;
        sprite.fades = false;
        sprites.push_back(sprite);
    }
    m_particles.drawSprites(sprites, effects::GroundFog::sheetOf(fog.settings()->sprite), false, view);
}

void PlayLevelEffects::drawWater() {
    if (m_effects != nullptr && m_engine.drawsPixels()) {
        m_water.draw(m_effects->water);
    }
}

void PlayLevelEffects::drawGlints(std::span<const effects::Particle> glints, const world::CameraPose& view) {
    if (m_engine.drawsPixels()) {
        m_particles.drawSprites(glints, effects::Triglint::kSheet, true, view);
    }
}

void PlayLevelEffects::drawOverlay(RenderEngine& engine) {
    if (m_effects != nullptr) {
        m_motionBlur.apply(engine, m_effects->motionBlur.current());
        m_smoke.draw(engine, m_effects->smoke);
    }
}

void PlayLevelEffects::drawScreenEffects(RenderEngine& engine) {
    if (m_effects == nullptr) {
        return;
    }
    if (const std::optional<int> passes = m_effects->blurPulse.passes(); passes && engine.drawsPixels()) {
        const effects::BlurPulse::Look& look = m_effects->blurPulse.look();
        engine.blurScreen(*passes, look.offsetU, look.offsetV);
    }
    drawTint(engine);
}

bool PlayLevelEffects::screenEffectsFirst() const { return m_effects != nullptr && m_effects->blurPulse.running(); }

void PlayLevelEffects::drawTint(RenderEngine& engine) {
    if (m_effects == nullptr || !m_effects->tint.drawn()) {
        return;
    }
    // The wash blends dst + (rgb - dst) x As / 128: the device's source alpha is that opacity on its 0-255 scale.
    const effects::ScreenTint::Colour colour = m_effects->tint.current();
    graphics::ScreenFade::drawWash(
        engine, graphics::Rgba{colour.r, colour.g, colour.b, effects::ScreenTint::deviceAlphaOf(colour.a)});
}

} // namespace coney::platform
