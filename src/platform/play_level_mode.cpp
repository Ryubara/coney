// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_level_mode.h"

#include <cmath>
#include <format>
#include <numbers>
#include <utility>

#include <rw.h>

#include "camera/camera_lens.h"
#include "characters/character_data.h"
#include "characters/character_rig.h"
#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "human/human_animator.h"

namespace coney::platform {

namespace {

// Game time in milliseconds from GameTimer ticks.
std::uint64_t millisecondsOf(std::uint64_t ticks) { return ticks / (GameTimer::kTicksPerSecond / 1000); }

// The gait's name for the summary.
const char* gaitName(human::Gait gait) {
    switch (gait) {
    case human::Gait::Standing:
        return "standing";
    case human::Gait::Sneak:
        return "sneak";
    case human::Gait::Walk:
        return "walk";
    case human::Gait::Jog:
        return "jog";
    case human::Gait::Run:
        return "run";
    case human::Gait::Sprint:
        return "sprint";
    }
    return "?";
}

} // namespace

std::expected<std::unique_ptr<PlayLevelMode>, Error>
PlayLevelMode::create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
                      std::function<void(std::string_view)> print) {
    auto scenery = LevelPlayScenery::load(engine, wad, name, budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    return createWith(engine, wad, std::move(*scenery), std::move(print));
}

std::expected<std::unique_ptr<PlayLevelMode>, Error>
PlayLevelMode::createInSandbox(RenderEngine& engine, const io::Wad& wad, sandbox::SandboxWorld world,
                               const std::optional<std::string>& spawn, std::function<void(std::string_view)> print) {
    auto scenery = SandboxPlayScenery::create(engine, std::move(world), spawn);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    return createWith(engine, wad, std::move(*scenery), std::move(print));
}

std::expected<std::unique_ptr<PlayLevelMode>, Error>
PlayLevelMode::createWith(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
                          std::function<void(std::string_view)> print) {
    // The player's character and its texture.
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    characters::addCharacterDataHandlers(table);
    addTextureDictionaryHandlers(table);
    auto character = human::PlayerCharacter::load(wad, table, human::kPlayerModel);
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
    // Where the player starts, which the scenery decides.
    const human::PlayerStart start = scenery->start();
    const human::Speeds speeds = human::speedsOf((*character)->anims(), human::AnimSlots::player());
    print(std::format("player: {} at ({:.2f}, {:.2f}, {:.2f}) heading {:.0f} ({}); speeds walk {:.3f}, jog {:.3f}, run "
                      "{:.3f}, sprint {:.3f} m/s\n",
                      human::kPlayerModel, start.position.x, start.position.y, start.position.z, start.headingDegrees,
                      scenery->startSource(), speeds.walk, speeds.jog, speeds.run, speeds.sprint));
    return std::unique_ptr<PlayLevelMode>(new PlayLevelMode(engine, std::move(scenery), std::move(*character),
                                                            std::move(*dictionaries), std::move(print)));
}

PlayLevelMode::PlayLevelMode(RenderEngine& engine, std::unique_ptr<PlayScenery> scenery,
                             std::unique_ptr<human::PlayerCharacter> character,
                             std::vector<TextureDictionary> dictionaries, std::function<void(std::string_view)> print)
    : m_engine(engine), m_scenery(std::move(scenery)), m_character(std::move(character)),
      m_dictionaries(std::move(dictionaries)),
      m_player(std::make_unique<human::Player>(*m_character, &m_scenery->collision(), m_scenery->start())),
      m_print(std::move(print)), m_positions(m_character->assets().model.vertices.size()),
      m_normals(m_character->assets().model.vertices.size()), m_drawDistance(m_scenery->drawDistance()) {
    // The texture: the character's dictionary holds one, which every material uses.
    rw::Texture* texture = nullptr;
    if (!m_dictionaries.empty()) {
        const std::vector<rw::Texture*> textures = m_dictionaries.front().textures();
        texture = textures.empty() ? nullptr : textures.front();
    }
    m_mesh = std::make_unique<CharacterMesh>(m_character->assets().model, texture);
    m_lights = std::make_unique<CharacterLights>(kCharacterAmbient, kCharacterDirectional,
                                                 directionToRenderWare(m_scenery->lightDirection()));
}

PlayLevelMode::~PlayLevelMode() {
    m_lights.reset();
    m_mesh.reset(); // before the dictionaries, whose texture it holds
}

WorldView PlayLevelMode::view(const human::PlayerSnapshot& snapshot, float drawDistance) const {
    // The follow camera through the player camera's lens, in RenderWare's axes; a window of another shape keeps the
    // view's height (as the world viewer does).
    const world::Vec3 position = toRenderWare(snapshot.cameraEye);
    const anim::Vec3 look = anim::subtract(snapshot.cameraTarget, snapshot.cameraEye);
    const anim::Vec3 forwardGame = anim::length(look) > 1e-6F ? anim::normalise(look) : anim::Vec3{0.0F, 1.0F, 0.0F};
    const anim::Vec3 forward = directionToRenderWare(forwardGame);
    const anim::Vec3 worldUp{0.0F, 1.0F, 0.0F};
    anim::Vec3 right = anim::cross(forward, worldUp);
    right = anim::length(right) > 1e-6F ? anim::normalise(right) : anim::Vec3{-1.0F, 0.0F, 0.0F};
    const anim::Vec3 up = anim::cross(right, forward);
    const camera::ViewWindow window = camera::viewWindow(camera::kPlayerCameraLens);
    const graphics::Extent size = m_engine.frameSize();
    const float aspect =
        size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 4.0F / 3.0F;
    return WorldView{.pose = world::CameraPose{.position = position,
                                               .forward = world::Vec3{forward.x, forward.y, forward.z},
                                               .up = world::Vec3{up.x, up.y, up.z},
                                               .right = world::Vec3{right.x, right.y, right.z}},
                     .halfWidth = window.halfHeight * aspect,
                     .halfHeight = window.halfHeight,
                     .nearClip = camera::kPlayerCameraLens.nearClip,
                     .drawDistance = drawDistance};
}

void PlayLevelMode::enter() { m_scenery->preload(toRenderWare(m_player->camera().position())); }

void PlayLevelMode::skin(const human::PlayerSnapshot& snapshot) {
    // The pose, skinned in the character's space (game axes, the feet at the origin, facing +y).
    const characters::CharacterModel& model = m_character->assets().model;
    const auto bones = anim::boneTransforms(m_character->skeleton(), snapshot.pose);
    const std::vector<anim::Mat34> matrices = characters::skinningMatrices(model, bones);
    characters::skinVertices(model, matrices, m_positions, m_normals);
    // Then placed: turned by the heading about z, moved to the feet, and into RenderWare's axes.
    const float c = std::cos(snapshot.heading);
    const float s = std::sin(snapshot.heading);
    const anim::Vec3 feet = snapshot.feet;
    for (std::size_t i = 0; i < m_positions.size(); ++i) {
        const anim::Vec3 p = m_positions[i];
        const anim::Vec3 n = m_normals[i];
        const anim::Vec3 placed{p.x * c - p.y * s + feet.x, p.x * s + p.y * c + feet.y, p.z + feet.z};
        m_positions[i] = anim::Vec3{placed.x, placed.z, -placed.y};
        m_normals[i] = directionToRenderWare(anim::Vec3{n.x * c - n.y * s, n.x * s + n.y * c, n.z});
    }
}

void PlayLevelMode::drawCharacter() const {
    m_lights->use();
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    m_mesh->atomic()->render();
}

ModeResult PlayLevelMode::update(GameModeStack& stack, const FrameTime& frame) {
    // The draw distance render() blends moves on a step.
    m_drawDistance.commit();

    // The characters' update, then the cameras' (human::Player keeps that order).
    const anim::Vec3 before = m_player->human().position();
    m_player->update(stack.pads().port(0), &m_scenery->collision());
    const anim::Vec3 after = m_player->human().position();
    m_stats.travelled += std::hypot(after.x - before.x, after.y - before.y);
    if (const std::uint32_t id = m_player->human().animator().animId(); id != m_lastAnimId) {
        m_print(std::format("frame {}: clip {}\n", frame.index, id));
        m_lastAnimId = id;
    }

    // The scenery's step around the camera (for a level: one streaming decision and the draw distance), then its
    // visibility pass from the newest step's camera: the next step's streaming reads it, so it belongs to the
    // simulation, not to the blended render.
    m_scenery->step(toRenderWare(m_player->camera().position()), frame);
    m_drawDistance.current() = m_scenery->drawDistance();
    m_scenery->findVisible(view(m_player->current(), m_drawDistance.current()));
    ++m_stats.frames;
    return ModeResult::Stay;
}

void PlayLevelMode::render(const RenderTime& time) {
    // Everything drawn comes from the player's snapshots and the draw distance, `alpha` of the way from the step before
    // to the newest one.
    const human::PlayerSnapshot snapshot = human::interpolate(m_player->previous(), m_player->current(), time.alpha);
    const WorldView blended = view(snapshot, lerp(m_drawDistance.previous(), m_drawDistance.current(), time.alpha));
    if (m_engine.drawsPixels()) {
        skin(snapshot);
        m_mesh->update(m_positions, m_normals);
    }
    // The scenery draws itself through the blended view, with the character among its objects.
    m_scenery->draw(m_engine, blended, millisecondsOf(time.gameTicks), [this] { drawCharacter(); });
}

std::string PlayLevelMode::summary() const {
    const human::Human& human = m_player->human();
    const anim::Vec3 p = human.position();
    const anim::Vec3 c = m_player->camera().position();
    return std::format(
        "play: {} frames, player at ({:.2f}, {:.2f}, {:.2f}) heading {:.1f} speed {:.2f} gait {} clip {} "
        "{}, travelled {:.2f} m, respawns {}; camera {:.2f} m away{}\n",
        m_stats.frames, p.x, p.y, p.z, human.heading() * 180.0F / std::numbers::pi_v<float>, human.speed(),
        gaitName(human.gait()), human.animator().animId(), human.airborne() ? "airborne" : "grounded",
        m_stats.travelled, m_player->respawns(), anim::distance(c, m_player->camera().lookAt()), m_scenery->summary());
}

} // namespace coney::platform
