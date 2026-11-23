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
        m_cars =
            std::make_unique<ParkedCars>(wad, *cars, engine.drawsPixels(), std::move(print), &engine.sceneAssets());
    }
}

void PlayLevelEffects::drawCars(const std::function<void(const ObjectDraw&)>& render, graphics::CarPass pass) {
    if (m_cars && m_engine.drawsScene()) {
        m_cars->draw(render, pass);
    }
}

void PlayLevelEffects::drawInScene(const world::CameraPose& view) {
    // The NULL backend draws nothing, so the sheets are never loaded there; GL3 and the GPU renderer draw them.
    if (m_effects != nullptr && m_engine.drawsScene()) {
        m_particles.draw(m_engine, m_effects->particles, view);
        drawFog(view);
    }
}

void PlayLevelEffects::drawFog(const world::CameraPose& view) {
    const effects::GroundFog& fog = m_effects->fog;
    const std::optional<effects::FogSettings>& settings = fog.settings();
    if (!settings) {
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
    m_particles.drawSprites(m_engine, sprites, effects::GroundFog::sheetOf(settings->sprite), false, view);
}

void PlayLevelEffects::drawWater() {
    if (m_effects != nullptr && m_engine.drawsScene()) {
        m_water.draw(m_engine, m_effects->water);
    }
}

void PlayLevelEffects::drawGlints(std::span<const effects::Particle> glints, const world::CameraPose& view) {
    if (m_engine.drawsScene()) {
        m_particles.drawSprites(m_engine, glints, effects::Triglint::kSheet, true, view);
    }
}

void PlayLevelEffects::drawOverlay(RenderEngine& engine, bool cardHeld) {
    if (m_effects != nullptr) {
        m_motionBlur.apply(engine, m_effects->motionBlur.current());
        // Only the drawing stops: the smoke keeps drifting under the card, as OverlayEffect_Tick still updates it.
        if (!cardHeld) {
            m_smoke.draw(engine, m_effects->smoke);
        }
    }
}

void PlayLevelEffects::drawScreenEffects(RenderEngine& engine) {
    // Stand-in: one view, player 1's. The original loops over its views, and with two views and a split screen
    // picks whose blur pulse, tint and motion blur to draw by their states; Coney has one set of screen effects.
    if (m_effects == nullptr) {
        return;
    }
    // The blur pulse, on every renderer that draws frames: GL3 and the GPU renderer, which draws it as GL3 does.
    if (const std::optional<int> passes = m_effects->blurPulse.passes(); passes && engine.drawsFrames()) {
        const effects::BlurPulse::Look& look = m_effects->blurPulse.look();
        engine.blurScreen(*passes, look.offsetU, look.offsetV);
    }
    // Then the tint. Stand-in: the fade and the letterbox the original steps and draws here are the scene stage's
    // (SceneStage::drawOverlay()), and the motion blur is drawn before the HUD from drawOverlay(), with no check of
    // the Rumble menu or the pad's state.
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
