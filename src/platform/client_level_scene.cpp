// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/client_level_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <utility>

#include <rw.h>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "camera/camera_lens.h"
#include "camera/camera_view.h"
#include "characters/character_data.h"
#include "characters/character_rig.h"
#include "core/game_timer.h"
#include "platform/draw_calls.h"
#include "platform/perf_probes.h"

namespace coney::platform {

namespace {

// The stand-in camera (Coney's choice, docs/guides/client-world.md#client-game): this far behind the followed human's
// feet and above them, looking at his chest, through the player camera's lens (camera::kPlayerCameraLens).
constexpr float kCameraBackMetres = 4.0F;
constexpr float kCameraUpMetres = 2.0F;
constexpr float kCameraLookUpMetres = 1.2F;

// The game time a frame shows, in milliseconds.
std::uint64_t millisecondsOf(std::uint64_t ticks) { return ticks / (GameTimer::kTicksPerSecond / 1000); }

// The texture a character's dictionaries hold, which every material uses (as the play mode's); null when none.
rw::Texture* textureOf(const std::vector<TextureDictionary>& dictionaries) {
    if (dictionaries.empty()) {
        return nullptr;
    }
    const std::vector<rw::Texture*> textures = dictionaries.front().textures();
    return textures.empty() ? nullptr : textures.front();
}

// The character `model` names and its textures from `wad`, converted for drawing when `engine` draws: as the play
// mode loads its player's (PlayLevelMode::loadCharacter()).
std::expected<std::pair<std::unique_ptr<human::PlayerCharacter>, std::vector<TextureDictionary>>, Error>
loadDrawnCharacter(const RenderEngine& engine, const io::Wad& wad, std::string_view model) {
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    characters::addCharacterDataHandlers(table);
    addTextureDictionaryHandlers(table);
    auto character = human::PlayerCharacter::load(wad, table, model);
    if (!character) {
        return std::unexpected(std::move(character.error()));
    }
    auto dictionaries = loadTextureDictionaries(wad, *(*character)->assets().textures, table);
    if (!dictionaries) {
        return std::unexpected(std::move(dictionaries.error()));
    }
    if (engine.drawsPixels()) {
        for (TextureDictionary& dictionary : *dictionaries) {
            if (auto converted = dictionary.convertForDrawing(); !converted) {
                return std::unexpected(std::move(converted.error()));
            }
        }
    }
    return std::pair{std::move(*character), std::move(*dictionaries)};
}

} // namespace

std::expected<std::unique_ptr<ClientLevelScene>, Error>
ClientLevelScene::load(RenderEngine& engine, const io::Wad& wad, const LevelAt& level, world::SectorBudget& budget,
                       std::function<void(std::string_view)> print) {
    auto scenery = LevelPlayScenery::load(engine, wad, level.level, budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    // Stand-in: every human as Rembrandt, since a snapshot does not say which character a human is.
    auto character = loadDrawnCharacter(engine, wad, human::kPlayerModel);
    if (!character) {
        return std::unexpected(std::move(character.error()));
    }
    std::unique_ptr<ClientLevelScene> scene(new ClientLevelScene(
        engine, wad, std::move(*scenery), std::move(character->first), std::move(character->second), std::move(print)));
    scene->m_scenery->preload(scene->startView().pose.position);
    return scene;
}

// The scene over what load() loaded: the character's clips and the stand-in lights made here.
ClientLevelScene::ClientLevelScene(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<LevelPlayScenery> scenery,
                                   std::unique_ptr<human::PlayerCharacter> character,
                                   std::vector<TextureDictionary> dictionaries,
                                   std::function<void(std::string_view)> print)
    : m_engine(engine), m_scenery(std::move(scenery)), m_character(std::move(character)),
      m_dictionaries(std::move(dictionaries)), m_print(std::move(print)) {
    m_clips = std::make_unique<net::CharacterClips>(m_character->anims());
    // The play mode's stand-in lights: the snapshots carry none of the level scripts' lighting.
    m_lights = std::make_unique<PlayLighting>(engine, wad, nullptr, m_scenery->lightDirection(), m_print);
    m_scenery->setLighting(&m_lights->scene());
    m_lastView = startView();
}

ClientLevelScene::~ClientLevelScene() {
    // The meshes hold the character's texture, and the scenery the lights: both go before what they point at.
    m_meshes.clear();
    if (m_scenery) {
        m_scenery->setLighting(nullptr);
    }
}

WorldView ClientLevelScene::viewOn(anim::Vec3 feet, float heading) const {
    // Heading 0 faces +y; the camera stands behind, along the heading turned about z.
    const anim::Vec3 forward{-std::sin(heading), std::cos(heading), 0.0F};
    const anim::Vec3 eye{feet.x - (forward.x * kCameraBackMetres), feet.y - (forward.y * kCameraBackMetres),
                         feet.z + kCameraUpMetres};
    const anim::Vec3 target{feet.x, feet.y, feet.z + kCameraLookUpMetres};
    const camera::CameraView view{.position = eye,
                                  .orientation = camera::lookRotation(anim::subtract(target, eye)),
                                  .lookAt = target,
                                  .fieldOfView = camera::kPlayerCameraLens.fieldOfView,
                                  .nearClip = camera::kPlayerCameraLens.nearClip,
                                  .farClip = camera::kPlayerCameraLens.farClip};
    return worldViewOf(view, m_engine.viewAspect(),
                       std::min(m_scenery->drawDistance(), camera::kPlayerCameraLens.farClip));
}

WorldView ClientLevelScene::startView() const {
    const human::PlayerStart start = m_scenery->start();
    return viewOn(start.position, start.headingDegrees * std::numbers::pi_v<float> / 180.0F);
}

void ClientLevelScene::step(const net::ClientWorld& world, const FrameTime& frame) {
    // The camera on the newest step's lowest id (the host's player 1), else the start; the scenery streams round it
    // and finds what it sees, as the play mode's step does.
    const net::ClientWorld::Humans& humans = world.humans();
    const WorldView before =
        humans.empty() ? startView() : viewOn(humans.begin()->second.position, humans.begin()->second.heading);
    m_scenery->step(before.pose.position, frame);
    const WorldView after =
        humans.empty() ? startView() : viewOn(humans.begin()->second.position, humans.begin()->second.heading);
    const anim::Vec3 feet = humans.empty() ? m_scenery->start().position : humans.begin()->second.position;
    m_lights->step(after, m_scenery->collision(), feet,
                   static_cast<std::uint32_t>(std::lround(frame.seconds * 1000.0)));
    m_scenery->findVisible(after);
}

void ClientLevelScene::render(const net::ClientWorld& world, const RenderTime& time) {
    const std::map<net::NetId, net::DrawnHuman> drawn = world.drawn(time.alpha);
    // A human gone from the world loses his mesh; a new one gets one.
    std::erase_if(m_meshes, [&drawn](const auto& entry) { return !drawn.contains(entry.first); });
    const characters::CharacterModel& model = m_character->assets().model;
    m_posed = 0;
    m_layersMissing = 0;
    for (const auto& [id, human] : drawn) {
        auto& mesh = m_meshes[id];
        if (!mesh) {
            mesh = std::make_unique<CharacterMesh>(model, textureOf(m_dictionaries), Skinning::Gpu,
                                                   &m_engine.sceneAssets());
        }
        // A layer whose clip the character lacks is left out of the pose; counted for the tests.
        for (std::size_t layer = 0; layer < net::playingLayers(human.state); ++layer) {
            if (m_clips->clip(human.state.layers.at(layer).clip) == nullptr) {
                ++m_layersMissing;
            }
        }
        const anim::Pose pose = net::humanPose(human.state, *m_clips, anim::referenceRotations());
        ++m_posed;
        // Posed wherever the scene is drawn, as the play mode poses its humans.
        if (m_engine.drawsScene()) {
            const auto bones = anim::boneTransforms(m_character->skeleton(), pose);
            mesh->pose(characters::skinningMatrices(model, bones),
                       characterPlacement(human.state.position, human.state.heading, 0.0F));
        }
        m_lights->addShadow(m_scenery->collision(), human.state.position);
    }
    m_lastView =
        drawn.empty() ? startView() : viewOn(drawn.begin()->second.state.position, drawn.begin()->second.state.heading);
    // The humans among the world's objects, lit as the play mode lights a human (a bare render of their atomics
    // crashes librw's lit pipeline); the blob shadows after the worlds.
    const auto drawObjects = [this] {
        const DrawKindScope kind(DrawKind::Humans);
        draw::setRenderState(rw::ZTESTENABLE, 1);
        draw::setRenderState(rw::ZWRITEENABLE, 1);
        draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
        SceneLighting& scene = m_lights->scene();
        for (auto& [id, mesh] : m_meshes) {
            scene.setMeshLayer(mesh->bloodAtomic());
            m_lights->drawHumanPasses(*mesh, false, nullptr);
            scene.setMeshLayer(nullptr);
        }
    };
    const auto afterWorlds = [this] {
        const DrawKindScope kind(DrawKind::Effects);
        m_lights->drawShadows();
    };
    const DrawKindScope kind(DrawKind::Scenery);
    m_scenery->draw(m_engine, m_lastView, millisecondsOf(time.gameTicks), drawObjects, afterWorlds);
}

} // namespace coney::platform
