// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_lighting.h"

#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include <rw.h>

#include "core/chunk_system.h"
#include "platform/draw_calls.h"
#include "platform/lit_atomic.h"
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
    static_cast<void>(lighting->lights.setOrigin(lighting->lights.addLight(ambient), graphics::LightOrigin::StandIn));
    graphics::LightDescriptor sun = ambient;
    sun.type = graphics::LightType::Directional;
    const float d = PlayLighting::kStandInDirectional;
    sun.colour = graphics::LightColour{d, d, d, 1.0F};
    sun.direction = graphics::gameToRenderWare(direction.x, direction.y, direction.z);
    static_cast<void>(lighting->lights.setOrigin(lighting->lights.addLight(sun), graphics::LightOrigin::StandIn));
    return lighting;
}

// A skinning matrix as a frame description's bone: its axes and translation, as given.
graphics::Transform3d boneOf(const anim::Mat34& m) {
    return graphics::Transform3d{.right = {m.x.x, m.x.y, m.x.z},
                                 .up = {m.y.x, m.y.y, m.y.z},
                                 .at = {m.z.x, m.z.y, m.z.z},
                                 .position = {m.t.x, m.t.y, m.t.z}};
}

// librw's vector as three floats.
std::array<float, 3> axisOf(const rw::V3d& v) { return {v.x, v.y, v.z}; }

} // namespace

PlayLighting::PlayLighting(RenderEngine& engine, const io::Wad& wad, graphics::LevelLighting* level,
                           anim::Vec3 standInDirection, const std::function<void(std::string_view)>& print)
    : m_engine(engine) {
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
    // The lighting lab's Hold lighting moves on with each step.
    m_scene->stepped();
}

void PlayLighting::drawHumanPasses(CharacterMesh& mesh, bool player, rw::Texture* blood) {
    rw::Atomic* atomic = mesh.atomic();
    // Faded out from 60 to 70 m from the camera, and not drawn beyond. **Coney's choice** (a stand-in): the distance is
    // the posed body's sphere centre's, and the first second's fade-in is not drawn (#1526).
    const rw::Sphere* sphere = atomic->getWorldBoundingSphere();
    const world::Vec3 eye = m_scene->pose().position;
    const float dx = sphere->center.x - eye.x;
    const float dy = sphere->center.y - eye.y;
    const float dz = sphere->center.z - eye.z;
    const float fade = graphics::humanDistanceFade(std::sqrt((dx * dx) + (dy * dy) + (dz * dz)));
    if (fade <= 0.0F) {
        mesh.setDrawnPasses(0);
        return;
    }
    // His materials' own colours (HuColor is white) times the player's shadow dimming, the alpha the fade; the blood
    // layer takes the same colours, so both passes are lit alike, as the original's vertex colours are equal.
    const graphics::Material3d material{.colour = graphics::MaterialColour::Shade,
                                        .fade = static_cast<std::uint8_t>(std::lround(255.0F * fade)),
                                        .shade = player ? m_playerDim.factor() : 1.0F};
    const bool hidden = player && m_playerDim.hidden();
    m_scene->noteHiddenHuman(hidden);
    if (blood != nullptr) {
        mesh.setBloodTexture(blood);
    }
    const std::size_t passes = blood != nullptr ? 2 : 1;
    const bool held = mesh.meshId().valid() && (blood == nullptr || mesh.bloodMeshId().valid());
    if (!held || m_scene->meshViewsOn()) {
        drawHumanInRig(mesh, material, hidden, blood);
        mesh.setDrawnPasses(passes);
        return;
    }
    // The frame description's draw: the body by id with the blood layer as its dual layer, the placement, the pose,
    // the colours, the humans' states as the play mode set them and his lights, selected once for both passes as the
    // original selects them once (docs/research/lighting.md#select); the GL3 renderer draws it at once.
    graphics::Draw3d draw;
    draw.mesh = mesh.meshId();
    if (blood != nullptr) {
        draw.dualLayer = mesh.bloodMeshId();
    }
    draw.dualPass = graphics::DualPass::Blood;
    const rw::Matrix* ltm = atomic->getFrame()->getLTM();
    draw.transform = graphics::Transform3d{
        .right = axisOf(ltm->right), .up = axisOf(ltm->up), .at = axisOf(ltm->at), .position = axisOf(ltm->pos)};
    draw.material = material;
    draw.states = sceneStatesInForce();
    std::vector<graphics::Transform3d> bones;
    bones.reserve(mesh.skinning().size());
    for (const anim::Mat34& matrix : mesh.skinning()) {
        bones.push_back(boneOf(matrix));
    }
    const std::vector<graphics::SceneLight> lights = m_scene->humanLights(atomic, hidden);
    m_engine.drawScene(draw, lights, bones, blood);
    mesh.setDrawnPasses(passes);
}

void PlayLighting::drawHumanInRig(CharacterMesh& mesh, const graphics::Material3d& material, bool hidden,
                                  rw::Texture* blood) {
    // Through the draw layer, so the draw-stream hashes see the humans (item 9 of docs/engine/renderer.md#order).
    const MaterialChange colours(*mesh.atomic(), material);
    m_scene->setRouted(true);
    m_scene->drawHumanAtomic(mesh.atomic(), hidden);
    if (blood != nullptr) {
        // GS context 2 (rendering.md#characters): ALPHA 0x44 (source over by the texture's alpha), alpha test off,
        // the same Z test with Z write. Equal depths pass librw's LEQUAL test.
        const MaterialChange layerColours(*mesh.bloodAtomic(), material);
        draw::setRenderState(rw::VERTEXALPHA, 1);
        draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
        m_scene->drawHumanAtomic(mesh.bloodAtomic(), hidden);
        draw::setRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
        draw::setRenderState(rw::VERTEXALPHA, 0);
    }
    m_scene->setRouted(false);
}

void PlayLighting::addShadow(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    if (auto shadow = graphics::placeBlobShadow(mesh, raycast::Vec3{feet.x, feet.y, feet.z})) {
        m_shadows.push_back(*shadow);
    }
}

void PlayLighting::drawShadows() {
    if (m_scene->drawsBlobShadows()) {
        m_scene->drawBlobShadows(m_shadows);
    }
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
