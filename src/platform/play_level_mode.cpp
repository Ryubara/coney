// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_level_mode.h"

#include <algorithm>
#include <array>
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
#include "world/view_frustum.h"
#include "world/world_streamer.h"

namespace coney::platform {

namespace {

// The stand-in start drops from this high above the first part's sectors.
constexpr float kStandInDrop = 500.0F;
// The character's directional light shines down and away from the usual camera side (game axes, z up).
constexpr anim::Vec3 kLightDirection{0.3F, 0.5F, -0.8F};

// Game time in milliseconds from GameTimer ticks.
std::uint64_t millisecondsOf(std::uint64_t ticks) { return ticks / (GameTimer::kTicksPerSecond / 1000); }

// A direction in RenderWare's axes, as toRenderWare() turns points.
anim::Vec3 directionToRenderWare(anim::Vec3 game) { return anim::Vec3{game.x, game.z, -game.y}; }

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

world::Vec3 toRenderWare(anim::Vec3 game) { return world::Vec3{game.x, game.z, -game.y}; }

human::PlayerStart playLevelStandInStart(const WorldSet& set, const raycast::CollisionMesh& mesh) {
    // The world viewer's start is in RenderWare's axes: back to the game's, (x, y, z) -> (x, -z, y).
    const world::Vec3 top = viewerStartPosition(*set.worlds().front());
    raycast::Vec3 point{top.x, -top.z, top.y + 1.0F};
    if (!raycast::dropToGround(mesh, kStandInDrop, point)) {
        point.z = top.y;
    }
    return human::PlayerStart{.position = anim::Vec3{point.x, point.y, point.z}, .headingDegrees = 0.0F};
}

std::expected<std::unique_ptr<PlayLevelMode>, Error>
PlayLevelMode::create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
                      std::function<void(std::string_view)> print) {
    // The scenery first, as LoadLevel reads it; a level needs its level file for the ground.
    auto scenery = loadLevelScenery(engine, wad, name, budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    if (!scenery->level || !scenery->level->collision) {
        return fail(ErrorCode::NotFound, std::format("{} has no level file to stand on", name));
    }
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
    // Where the player starts.
    const std::optional<human::PlayerStart> researched = human::researchedPlayerStart(name);
    const human::PlayerStart start =
        researched ? *researched : playLevelStandInStart(*scenery->set, *scenery->level->collision);
    const human::Speeds speeds = human::speedsOf((*character)->anims(), human::AnimSlots::player());
    print(std::format("player: {} at ({:.2f}, {:.2f}, {:.2f}) heading {:.0f} ({}); speeds walk {:.3f}, jog {:.3f}, run "
                      "{:.3f}, sprint {:.3f} m/s\n",
                      human::kPlayerModel, start.position.x, start.position.y, start.position.z, start.headingDegrees,
                      researched ? "the level's start" : "Coney's stand-in start", speeds.walk, speeds.jog, speeds.run,
                      speeds.sprint));
    return std::unique_ptr<PlayLevelMode>(new PlayLevelMode(engine, std::move(*scenery), std::move(*character),
                                                            std::move(*dictionaries), budget, start, std::move(print)));
}

PlayLevelMode::PlayLevelMode(RenderEngine& engine, LevelScenery scenery,
                             std::unique_ptr<human::PlayerCharacter> character,
                             std::vector<TextureDictionary> dictionaries, world::SectorBudget& budget,
                             const human::PlayerStart& start, std::function<void(std::string_view)> print)
    : m_engine(engine), m_scenery(std::move(scenery)), m_character(std::move(character)),
      m_dictionaries(std::move(dictionaries)), m_budget(budget),
      m_player(std::make_unique<human::Player>(*m_character, m_scenery.level->collision.get(), start)),
      m_renderer(WorldViewerMode::kAmbient), m_print(std::move(print)),
      m_positions(m_character->assets().model.vertices.size()), m_normals(m_character->assets().model.vertices.size()),
      m_drawDistance(camera::kPlayerCameraLens.farClip) {
    // The texture: the character's dictionary holds one, which every material uses.
    rw::Texture* texture = nullptr;
    if (!m_dictionaries.empty()) {
        const std::vector<rw::Texture*> textures = m_dictionaries.front().textures();
        texture = textures.empty() ? nullptr : textures.front();
    }
    m_mesh = std::make_unique<CharacterMesh>(m_character->assets().model, texture);
    m_lights = std::make_unique<CharacterLights>(kCharacterAmbient, kCharacterDirectional,
                                                 directionToRenderWare(kLightDirection));
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

void PlayLevelMode::enter() {
    const std::array<world::Vec3, 1> cameras{toRenderWare(m_player->camera().position())};
    const world::PreloadResult preload =
        world::preloadWorlds(m_scenery.set->worlds(), cameras, m_drawDistance.current(), m_budget, *m_scenery.set, 0);
    m_stats.unloads += preload.unloaded;
    m_stats.failures += preload.failed;
    m_print(std::format("preload: {} parts read, {} freed, {} failed; {} atomics resident\n", preload.loaded,
                        preload.unloaded, preload.failed, m_scenery.set->residentAtomics()));
}

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
    const auto seconds = static_cast<float>(frame.seconds);
    m_drawDistance.commit();
    const std::uint64_t nowMs = millisecondsOf(frame.gameTicks);
    const raycast::CollisionMesh* mesh = m_scenery.level->collision.get();

    // The characters' update, then the cameras' (human::Player keeps that order).
    const anim::Vec3 before = m_player->human().position();
    m_player->update(stack.pads().port(0), mesh);
    const anim::Vec3 after = m_player->human().position();
    m_stats.travelled += std::hypot(after.x - before.x, after.y - before.y);
    if (const std::uint32_t id = m_player->human().animator().animId(); id != m_lastAnimId) {
        m_print(std::format("frame {}: clip {}\n", frame.index, id));
        m_lastAnimId = id;
    }

    // One streaming decision around the camera, then the draw distance.
    WorldSet& set = *m_scenery.set;
    const std::array<world::Vec3, 1> cameras{toRenderWare(m_player->camera().position())};
    const world::StreamStep step =
        world::updateStreaming(set.worlds(), cameras, m_drawDistance.current(), m_budget, set, nowMs);
    switch (step.result) {
    case world::StreamResult::Loaded:
        ++m_stats.loads;
        break;
    case world::StreamResult::Unloaded:
        ++m_stats.unloads;
        break;
    case world::StreamResult::Failed:
        ++m_stats.failures;
        m_print(std::format("frame {}: part {} failed: {}\n", frame.index, step.part, step.error));
        break;
    case world::StreamResult::NoRoom:
    case world::StreamResult::Idle:
        break;
    }
    m_pending = world::nearestPendingDistance(set.worlds(), cameras);
    m_drawDistance.current() = world::adjustDrawDistance(
        m_drawDistance.current(), world::DrawDistanceInputs{.pending = m_pending,
                                                            .farClip = camera::kPlayerCameraLens.farClip,
                                                            .seconds = seconds,
                                                            .frameRate = 30.0F,
                                                            .viewports = 1,
                                                            .lowRateMode = false});

    // The visibility pass, from the newest step's camera: the next step's streaming reads it, so it belongs to the
    // simulation, not to the blended render.
    const WorldView newest = view(m_player->current(), m_drawDistance.current());
    const world::ViewFrustum frustum(newest.pose, newest.halfWidth, newest.halfHeight, newest.nearClip,
                                     newest.drawDistance);
    for (world::StreamedWorld* world : set.worlds()) {
        world->findVisibleSectors(frustum, true);
    }
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
    // The world draws the sectors its own view sees (WorldRenderer::render), with the character among the objects.
    m_renderer.render(m_engine, *m_scenery.set, m_scenery.level.get(), blended, kFogColour, m_pending,
                      millisecondsOf(time.gameTicks), [this] { drawCharacter(); });
}

std::string PlayLevelMode::summary() const {
    const human::Human& human = m_player->human();
    const anim::Vec3 p = human.position();
    const anim::Vec3 c = m_player->camera().position();
    return std::format(
        "play: {} frames, player at ({:.2f}, {:.2f}, {:.2f}) heading {:.1f} speed {:.2f} gait {} clip {} "
        "{}, travelled {:.2f} m, respawns {}; camera {:.2f} m away; parts read {}, freed {}, failed {}\n",
        m_stats.frames, p.x, p.y, p.z, human.heading() * 180.0F / std::numbers::pi_v<float>, human.speed(),
        gaitName(human.gait()), human.animator().animId(), human.airborne() ? "airborne" : "grounded",
        m_stats.travelled, m_player->respawns(), anim::distance(c, m_player->camera().lookAt()), m_stats.loads,
        m_stats.unloads, m_stats.failures);
}

} // namespace coney::platform
