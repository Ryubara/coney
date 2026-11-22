// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/particle_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

#include "graphics/frame_description.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

namespace {

// Whether sprites of `behaviour` are lights, added to what is behind them, rather than blended by their alpha.
bool additive(effects::ParticleBehaviour behaviour) {
    switch (behaviour) {
    case effects::ParticleBehaviour::Glow:
    case effects::ParticleBehaviour::Flash:
    case effects::ParticleBehaviour::Flames:
    case effects::ParticleBehaviour::Sparks:
    case effects::ParticleBehaviour::Embers:
        return true;
    case effects::ParticleBehaviour::Inert:
    case effects::ParticleBehaviour::Puff:
    case effects::ParticleBehaviour::Steam:
    case effects::ParticleBehaviour::Strobe:
    case effects::ParticleBehaviour::Neon:
    case effects::ParticleBehaviour::Flies:
    case effects::ParticleBehaviour::Spray:
    case effects::ParticleBehaviour::Shard:
    case effects::ParticleBehaviour::Explode:
    case effects::ParticleBehaviour::Explosion:
    case effects::ParticleBehaviour::Fireball:
    case effects::ParticleBehaviour::Debris:
        return false;
    }
    return false;
}

// The byte of `colour` (`0xRRGGBBAA`) `shift` bits up.
std::uint8_t channel(std::uint32_t colour, unsigned shift) { return static_cast<std::uint8_t>(colour >> shift); }

// How the sprites draw: depth tested but not written, both faces, filtered and clamped, and no distance fog: the
// original gives every sprite vertex the fixed fog value 254 of 255, so a sprite takes 1/255 of the fog colour wherever
// it is (docs/research/rendering.md#sprites), which Coney rounds to none. The glows, flashes, flames and sparks add to
// what is behind them (`add`), the rest blend by alpha. After a draw the states the solid draws expect are put back:
// the fog as it was, blending by alpha, depth written, back faces dropped.
graphics::EffectDraw spriteDraw(bool add) {
    graphics::EffectDraw draw;
    draw.states.fog = false;
    draw.states.depthTest = true;
    draw.states.depthWrite = false;
    draw.states.vertexAlpha = true;
    draw.states.cull = graphics::CullFaces::None;
    draw.states.filter = graphics::TextureFilter::Linear;
    draw.states.address = graphics::TextureAddress::Clamp;
    draw.states.blend = add ? graphics::EffectBlend::Additive : graphics::EffectBlend::Alpha;
    draw.restore = graphics::EffectRestore{.fog = true, .blend = true, .depthWrite = true, .cull = true};
    return draw;
}

// The sprites of one draw: one sheet, one blend, two triangles each.
struct Batch {
    std::vector<graphics::EffectVertex> vertices;
    std::vector<std::uint16_t> indices;

    // Adds the square `particle` makes, `half` its half-size along the camera's `right` and `up`, showing `uv`.
    void add(const effects::Particle& particle, world::Vec3 right, world::Vec3 up, const graphics::UvRect& uv,
             std::uint8_t alpha) {
        const world::Vec3 centre = toRenderWare(particle.position);
        const float half = particle.size * 0.5F;
        const float c = std::cos(particle.angle) * half;
        const float s = std::sin(particle.angle) * half;
        // The square's two half-axes on the screen, turned by the sprite's angle.
        const world::Vec3 a{right.x * c + up.x * s, right.y * c + up.y * s, right.z * c + up.z * s};
        const world::Vec3 b{up.x * c - right.x * s, up.y * c - right.y * s, up.z * c - right.z * s};
        const auto base = static_cast<std::uint16_t>(vertices.size());
        const std::array<std::array<float, 4>, 4> corners{{{-1.0F, 1.0F, uv.u0, uv.v0},
                                                           {1.0F, 1.0F, uv.u1, uv.v0},
                                                           {1.0F, -1.0F, uv.u1, uv.v1},
                                                           {-1.0F, -1.0F, uv.u0, uv.v1}}};
        for (const auto& corner : corners) {
            graphics::EffectVertex vertex;
            vertex.x = centre.x + a.x * corner[0] + b.x * corner[1];
            vertex.y = centre.y + a.y * corner[0] + b.y * corner[1];
            vertex.z = centre.z + a.z * corner[0] + b.z * corner[1];
            vertex.colour = graphics::Rgba{channel(particle.colour, 24), channel(particle.colour, 16),
                                           channel(particle.colour, 8), alpha};
            vertex.u = corner[2];
            vertex.v = corner[3];
            vertices.push_back(vertex);
        }
        constexpr std::array<std::uint16_t, 6> kCorners{0, 1, 2, 0, 2, 3};
        for (const std::uint16_t i : kCorners) {
            indices.push_back(static_cast<std::uint16_t>(base + i));
        }
    }

    // Draws the batch with `texture` (null: untextured), then empties it.
    void flush(RenderEngine& engine, const graphics::Texture* texture, bool add) {
        if (vertices.empty()) {
            return;
        }
        engine.drawEffect(spriteDraw(add), vertices, indices, texture);
        vertices.clear();
        indices.clear();
    }
};

// The most sprites one batch holds: its indices are 16-bit.
constexpr std::size_t kBatchSprites = 16000;

} // namespace

ParticleRenderer::ParticleRenderer(const io::Wad& wad, bool forDrawing, std::function<void(std::string_view)> print)
    : m_wad(wad), m_forDrawing(forDrawing), m_print(std::move(print)),
      m_table(chunk::ChunkHandlerTable::withDefaults()) {
    addTextureDictionaryHandlers(m_table);
    addSpriteSheetHandlers(m_table);
}

ParticleRenderer::~ParticleRenderer() = default;

const graphics::SpriteSheet* ParticleRenderer::sheet(effects::ParticleSheet which) {
    if (which == effects::ParticleSheet::None) {
        return nullptr;
    }
    if (const auto found = m_sheets.find(which); found != m_sheets.end()) {
        return found->second.get();
    }
    std::unique_ptr<graphics::SpriteSheet>& slot = m_sheets[which];
    auto loaded = loadSpriteSheetResource(m_wad, m_table, effects::sheetName(which), m_forDrawing);
    if (!loaded) {
        if (m_print) {
            m_print(
                std::format("particles: sheet {} not loaded: {}\n", effects::sheetName(which), loaded.error().message));
        }
        return nullptr;
    }
    slot = std::make_unique<graphics::SpriteSheet>(std::move(*loaded));
    return slot.get();
}

void ParticleRenderer::useSheet(effects::ParticleSheet which, graphics::SpriteSheet sheet) {
    m_sheets[which] = std::make_unique<graphics::SpriteSheet>(std::move(sheet));
}

void ParticleRenderer::draw(RenderEngine& engine, const effects::ParticleSystems& systems,
                            const world::CameraPose& view) {
    if (systems.particleCount() == 0) {
        return;
    }
    // The screen's right and up in the world: the camera pose's (its right is forward x up).
    const world::Vec3 right = view.right;
    const world::Vec3 up = view.up;
    Batch batch;
    for (const effects::ParticleSystem& system : systems.systems()) {
        if (system.hidden || system.particles.empty()) {
            continue;
        }
        const graphics::SpriteSheet* spriteSheet = sheet(system.type->sheet);
        const graphics::Texture* texture = spriteSheet != nullptr ? spriteSheet->texture.get() : nullptr;
        for (const effects::Particle& particle : system.particles) {
            // A sprite fades out over its life when its type says so.
            float alpha = static_cast<float>(channel(particle.colour, 0));
            if (particle.fades && particle.life > 0.0F) {
                alpha *= std::max(0.0F, 1.0F - particle.age / particle.life);
            }
            const graphics::UvRect uv = spriteSheet != nullptr && particle.rect < spriteSheet->page.rects.size()
                                            ? spriteSheet->page.rect(particle.rect)
                                            : graphics::UvRect{};
            batch.add(particle, right, up, uv, static_cast<std::uint8_t>(alpha));
            if (batch.indices.size() >= kBatchSprites * 6) {
                batch.flush(engine, texture, additive(system.type->behaviour));
            }
        }
        // One system's sprites share a sheet and a blend: draw them together.
        batch.flush(engine, texture, additive(system.type->behaviour));
    }
}

void ParticleRenderer::drawSprites(RenderEngine& engine, std::span<const effects::Particle> sprites,
                                   effects::ParticleSheet sheetOf, bool add, const world::CameraPose& view) {
    if (sprites.empty()) {
        return;
    }
    const graphics::SpriteSheet* spriteSheet = sheet(sheetOf);
    const graphics::Texture* texture = spriteSheet != nullptr ? spriteSheet->texture.get() : nullptr;
    Batch batch;
    for (const effects::Particle& sprite : sprites) {
        const graphics::UvRect uv = spriteSheet != nullptr && sprite.rect < spriteSheet->page.rects.size()
                                        ? spriteSheet->page.rect(sprite.rect)
                                        : graphics::UvRect{};
        batch.add(sprite, view.right, view.up, uv, channel(sprite.colour, 0));
        if (batch.indices.size() >= kBatchSprites * 6) {
            batch.flush(engine, texture, add);
        }
    }
    batch.flush(engine, texture, add);
}

} // namespace coney::platform
