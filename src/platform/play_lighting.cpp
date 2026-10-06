// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_lighting.h"

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

#include <rw.h>

#include "core/chunk_system.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

namespace {

// The sheets of the coronas and of the blob shadows.
constexpr std::string_view kCoronaSheet = "lighting";
constexpr std::string_view kShadowSheet = "part_page1";

// Loads sheet `name` for drawing, or nothing (with a line to `print`) when it does not load.
std::optional<graphics::SpriteSheet> loadSheet(RenderEngine& engine, const io::Wad& wad,
                                               const chunk::ChunkHandlerTable& table, std::string_view name,
                                               const std::function<void(std::string_view)>& print) {
    if (!engine.drawsPixels()) {
        return std::nullopt;
    }
    auto sheet = loadSpriteSheetResource(wad, table, name, true);
    if (!sheet) {
        print(std::format("lighting: sheet {} not loaded: {}\n", name, sheet.error().message));
        return std::nullopt;
    }
    return std::move(*sheet);
}

// The stand-in lighting of a scenery without a level's: an ambient and a directional light for objects.
std::unique_ptr<graphics::LevelLighting> standIn(anim::Vec3 direction) {
    auto lighting = std::make_unique<graphics::LevelLighting>();
    graphics::LightDescriptor ambient;
    ambient.type = graphics::LightType::Ambient;
    const float a = PlayLighting::kStandInAmbient;
    ambient.colour = graphics::LightColour{a, a, a, 1.0F};
    ambient.lights = graphics::kLightsObjects;
    lighting->lights.addLight(ambient);
    graphics::LightDescriptor sun = ambient;
    sun.type = graphics::LightType::Directional;
    const float d = PlayLighting::kStandInDirectional;
    sun.colour = graphics::LightColour{d, d, d, 1.0F};
    sun.direction = graphics::gameToRenderWare(direction.x, direction.y, direction.z);
    lighting->lights.addLight(sun);
    return lighting;
}

// A colour channel scaled by `factor`.
rw::uint8 scaledChannel(rw::uint8 channel, float factor) {
    return static_cast<rw::uint8>(static_cast<float>(channel) * std::clamp(factor, 0.0F, 1.0F));
}

} // namespace

PlayLighting::PlayLighting(RenderEngine& engine, const io::Wad& wad, graphics::LevelLighting* level,
                           anim::Vec3 standInDirection, const std::function<void(std::string_view)>& print) {
    if (level == nullptr) {
        m_own = standIn(standInDirection);
        level = m_own.get();
    }
    m_level = level;
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    addTextureDictionaryHandlers(table);
    addSpriteSheetHandlers(table);
    m_scene = std::make_unique<SceneLighting>(*m_level, loadSheet(engine, wad, table, kCoronaSheet, print),
                                              loadSheet(engine, wad, table, kShadowSheet, print));
}

PlayLighting::~PlayLighting() = default;

void PlayLighting::step(const WorldView& view, const raycast::CollisionMesh& mesh, anim::Vec3 playerFeet,
                        std::uint32_t elapsedMs) {
    m_level->lights.advance(graphics::LightView{.position = view.pose.position,
                                                .forward = view.pose.forward,
                                                .nearClip = view.nearClip,
                                                .farClip = view.drawDistance},
                            elapsedMs);
    m_playerDim.step(graphics::onShadowGround(mesh, raycast::Vec3{playerFeet.x, playerFeet.y, playerFeet.z}),
                     elapsedMs);
}

void PlayLighting::drawHuman(rw::Atomic* atomic, bool player) {
    if (player) {
        // The player's materials keep their own colours, times the dimming (HuColor is white).
        rw::Geometry* geometry = atomic->geometry;
        if (atomic != m_playerAtomic ||
            m_playerColours.size() != static_cast<std::size_t>(geometry->matList.numMaterials)) {
            m_playerAtomic = atomic;
            m_playerColours.clear();
            for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
                m_playerColours.push_back(geometry->matList.materials[i]->color);
            }
        }
        const float factor = m_playerDim.factor();
        for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
            const rw::RGBA base = m_playerColours.at(static_cast<std::size_t>(i));
            rw::RGBA& colour = geometry->matList.materials[i]->color;
            colour.red = scaledChannel(base.red, factor);
            colour.green = scaledChannel(base.green, factor);
            colour.blue = scaledChannel(base.blue, factor);
        }
    }
    m_scene->drawHumanAtomic(atomic, player && m_playerDim.hidden());
}

void PlayLighting::addShadow(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    if (auto shadow = graphics::placeBlobShadow(mesh, raycast::Vec3{feet.x, feet.y, feet.z})) {
        m_shadows.push_back(*shadow);
    }
}

void PlayLighting::drawShadows() {
    m_scene->drawBlobShadows(m_shadows);
    m_shadows.clear();
}

std::string PlayLighting::summary() const {
    const graphics::LightManager& lights = m_level->lights;
    const graphics::LightColour ambient = lights.worldAmbientBase();
    const graphics::LightColour added = lights.addedColour();
    const graphics::WorldFog& fog = m_level->fog;
    return std::format("lighting: {} lights{}, world ambient ({:.3f}, {:.3f}, {:.3f}), fog ({}, {}, {}) from {:.2f}\n",
                       lights.count(), m_own ? " (Coney's stand-in)" : "", ambient.r + added.r, ambient.g + added.g,
                       ambient.b + added.b, fog.colour.r, fog.colour.g, fog.colour.b, fog.start);
}

} // namespace coney::platform
