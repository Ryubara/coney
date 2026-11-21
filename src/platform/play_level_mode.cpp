// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_level_mode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rw.h>

#include "ai/brain.h"
#include "animation/anim_pose.h"
#include "camera/camera_lens.h"
#include "camera/camera_view.h"
#include "camera/cameras.h"
#include "characters/character_data.h"
#include "characters/character_rig.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/perf.h"
#include "fileio/executable.h"
#include "graphics/human_blood.h"
#include "human/human_animator.h"
#include "human/player_trace.h"
#include "net/game_humans.h"
#include "platform/draw_calls.h"
#include "platform/object_draw.h"
#include "platform/perf_probes.h"
#include "platform/play_level_effects.h"
#include "raycast/collision_mesh.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"

namespace coney::platform {

namespace {

// Game time in milliseconds from GameTimer ticks.
std::uint64_t millisecondsOf(std::uint64_t ticks) { return ticks / (GameTimer::kTicksPerSecond / 1000); }

// How far above a teleport's spot the drop onto the ground starts, and how far below it reaches, in metres.
constexpr float kTeleportDrop = 2.0F;
// How fast the free camera flies, in metres a second: the sandbox viewer's walking pace for a level's scale.
constexpr float kFreeCameraSpeed = 8.0F;
// How far debug lines are lifted off what they lie on, in metres.
constexpr float kLineLift = 0.02F;

// A coloured line list for librw's 3D immediate mode, in RenderWare's axes.
class LineList {
  public:
    // Adds a line from `a` to `b` (game axes) in `colour`.
    void add(anim::Vec3 a, anim::Vec3 b, graphics::Rgba colour) {
        push(a, colour);
        push(b, colour);
    }
    // Draws the lines through the current camera: tested against depth, not writing it, untextured.
    void draw() {
        if (m_vertices.empty()) {
            return;
        }
        draw::setRenderState(rw::ZTESTENABLE, 1);
        draw::setRenderState(rw::ZWRITEENABLE, 0);
        draw::setRenderState(rw::VERTEXALPHA, 0);
        draw::setRenderStatePtr(rw::TEXTURERASTER, nullptr);
        draw::im3dTransform(m_vertices.data(), static_cast<rw::int32>(m_vertices.size()), nullptr,
                            rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA);
        draw::im3dRenderPrimitive(rw::PRIMTYPELINELIST);
        draw::im3dEnd();
        draw::setRenderState(rw::ZWRITEENABLE, 1);
    }

  private:
    // One vertex, lifted a little so a line on a surface is not hidden in it, turned into RenderWare's axes.
    void push(anim::Vec3 game, graphics::Rgba colour) {
        const world::Vec3 p = toRenderWare(anim::Vec3{game.x, game.y, game.z + kLineLift});
        rw::gl3::Im3DVertex vertex{};
        vertex.setX(p.x);
        vertex.setY(p.y);
        vertex.setZ(p.z);
        vertex.setColor(colour.r, colour.g, colour.b, colour.a);
        m_vertices.push_back(vertex);
    }

    std::vector<rw::gl3::Im3DVertex> m_vertices;
};

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
                      std::function<void(std::string_view)> print, std::optional<human::PlayerStart> start,
                      const PlayerSetup& setup, const ScriptedCast* cast) {
    auto scenery = LevelPlayScenery::load(engine, wad, name, budget, print, start);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    // A start that is not the level script's (the researched or stand-in start) is snapped as a creation is.
    PlayerSetup used = setup;
    used.snapToGround = setup.snapToGround || !start;
    return createWith(engine, wad, std::move(*scenery), std::move(print), used, cast);
}

std::expected<std::unique_ptr<PlayLevelMode>, Error>
PlayLevelMode::createWith(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
                          std::function<void(std::string_view)> print, const PlayerSetup& setup,
                          const ScriptedCast* cast) {
    // The player's character and its texture: the model the level script's type names, else Rembrandt's.
    std::string model = setup.model.empty() ? std::string(human::kPlayerModel) : setup.model;
    bool fellBack = false;
    auto loaded = loadCharacter(engine, wad, model);
    if (!loaded && model != human::kPlayerModel) {
        print(std::format("player: model {} could not be loaded ({}); playing {} instead\n", model,
                          loaded.error().message, human::kPlayerModel));
        model = human::kPlayerModel;
        loaded = loadCharacter(engine, wad, model);
        fellBack = true;
    }
    if (!loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    // Rembrandt in place of a model that failed is type 32.
    PlayerSetup used = setup;
    if (fellBack) {
        used.type = human::kPlayerType;
    }
    // Where the player starts, which the scenery decides.
    const human::PlayerStart start = scenery->start();
    const human::Speeds speeds = human::speedsOf(loaded->character->anims(), human::AnimSlots::player());
    print(std::format("player: {} at ({:.2f}, {:.2f}, {:.2f}) heading {:.0f} ({}{}); speeds walk {:.3f}, jog {:.3f}, "
                      "run {:.3f}, sprint {:.3f} m/s\n",
                      model, start.position.x, start.position.y, start.position.z, start.headingDegrees,
                      scenery->startSource(), setup.snapToGround ? "" : ", not snapped", speeds.walk, speeds.jog,
                      speeds.run, speeds.sprint));
    // The level's cars, particles and motion blur, when gameplay brought them. A car adds nothing to the collision:
    // humans meet and climb it through its shell in the level's mesh (docs/research/characters.md#car-shells).
    std::unique_ptr<PlayLevelEffects> levelEffects;
    if (cast != nullptr && (cast->effects != nullptr || cast->cars != nullptr)) {
        levelEffects = std::make_unique<PlayLevelEffects>(engine, wad, cast->effects, cast->cars, print);
    }
    return std::unique_ptr<PlayLevelMode>(new PlayLevelMode(engine, wad, std::move(scenery), std::move(*loaded),
                                                            std::move(print), std::move(model), used, cast,
                                                            std::move(levelEffects)));
}

// The level's scenery, the player's character and mesh, the lights, the cast and the AI, ready for the first step.
PlayLevelMode::PlayLevelMode(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
                             LoadedCharacter loaded, std::function<void(std::string_view)> print, std::string model,
                             const PlayerSetup& setup, const ScriptedCast* cast,
                             std::unique_ptr<PlayLevelEffects> levelEffects)
    : PlayLevelSimulation(wad, std::move(scenery), std::move(loaded.character), std::move(print), std::move(model),
                          setup.types, setup.type, setup.snapToGround),
      // The ground the simulation was given is this scenery.
      m_engine(engine), m_playScenery(static_cast<PlayScenery&>(*m_scenery)),
      m_dictionaries(std::move(loaded.dictionaries)), m_drawDistance(m_playScenery.drawDistance()) {
    m_texture = textureOf(m_dictionaries);
    m_sceneModel = m_model;
    // The blood of every human's second pass; without it humans are drawn in one pass.
    if (auto blood = BloodTextures::load(engine, wad); blood) {
        m_blood = std::move(*blood);
    } else {
        m_print(std::format("humans: no blood textures ({}); drawn without them\n", blood.error().message));
    }
    m_mesh =
        std::make_unique<CharacterMesh>(m_character->assets().model, m_texture, Skinning::Gpu, &m_engine.sceneAssets());
    // The level's lights as its scripts set them, or a stand-in; the scenery draws with them too.
    m_lights = std::make_unique<PlayLighting>(engine, wad, cast != nullptr ? cast->lighting : nullptr,
                                              m_playScenery.lightDirection(), m_print);
    m_playScenery.setLighting(&m_lights->scene());
    m_print(m_lights->summary());
    // The layout's targets, each drawn with its own copy of the character's mesh.
    makeTargets();
    for (const Target& target : m_targets) {
        m_targetMeshes.emplace(target.human.get(),
                               std::make_unique<CharacterMesh>(m_character->assets().model, m_texture, Skinning::Gpu,
                                                               &m_engine.sceneAssets()));
    }
    // The glints' blink draws from the game's random table when the disc's executable has the NTSC-U one.
    if (auto table = io::readExecutableWords(wad.disc(), GameRandom::kExecutableName, GameRandom::kTableAddress,
                                             GameRandom::kTableSize)) {
        m_glints.setTable(*table);
        m_player->humans().setRandomTable(*table);
    }
    // The brains plan their moves on the level's routes, when it has path data.
    makePlanner();
    if (cast != nullptr) {
        bindObjects(cast->objects, cast->recorded);
        m_cars = cast->cars;
        bindPickups(cast->pickups);
        m_sound = cast->sound;
        m_gameOver = cast->gameOver;
        m_padOptions = cast->padOptions;
        m_difficulty = cast->difficulty;
        m_missionFailed = cast->missionFailed;
        makeWorldObjects(*cast);
    }
    // The AI humans in the player's step: the level's scripts' humans, or the layout's fighters.
    if (cast != nullptr && cast->brains != nullptr && cast->scripted != nullptr) {
        makeCast(*cast, setup.ai);
    } else {
        makeLayoutFighters(setup.ai);
    }
    // The HUD, with the player on panel 0 (docs/research/hud.md#the-player-panel).
    m_hudLayer = HudLayer::create(*m_hud, wad, engine.drawsPixels(), m_print);
    m_hud->hud().attachPlayer(0, m_type);
    makeStage();
    // The puppets are drawn from the frame description, their meshes held by id
    // (docs/engine/renderer.md#frame-description).
    m_stage->setSceneAssets(&m_engine.sceneAssets());
    // Whose face clips show (Human_UsesQueuedPose, docs/research/face.md): nobody a scene holds; an AI
    // Warrior always; anyone else within 17.5 m of the camera. The original measures to the nearest camera
    // (Human_UpdateLod's +0x334); Coney has player 1's alone.
    m_player->humans().setFaceShown([this](const human::Human& human) {
        if (m_scenes != nullptr && m_stage != nullptr && m_stage->holds(handleOf(human))) {
            return false;
        }
        if (m_ai) {
            if (const ai::Brain* brain = m_ai->brains().find(human);
                brain != nullptr && brain->type() == ai::BrainType::Warrior) {
                return true;
            }
        }
        return anim::distance(human.position(), m_player->camera().position()) < human::kFaceShownDistance;
    });
    // A scene's camera is player 1's scene camera, pushed over the camera shown and popped back at its end.
    if (cast != nullptr) {
        m_stage->setCameras(cast->cameras);
    }
    // The level's scenes play on the stage, player 1 being the human the level's scripts made him.
    if (cast != nullptr && cast->scenes != nullptr) {
        const HumanCreation* player = cast->humans != nullptr ? cast->humans->player(1) : nullptr;
        attachScenes(cast->scenes, player != nullptr ? player->handle : 0.0);
    }
    m_levelEffects = std::move(levelEffects);
}

PlayLevelMode::~PlayLevelMode() {
    // The level's humans go with it; network play must not hold them.
    if (m_netHumans != nullptr) {
        m_netHumans->clear();
    }
    // The dynamic clips go with the mode; the scripts' hold (gameplay's, which outlives it) must not use them.
    if (m_cast.scripted != nullptr) {
        m_cast.scripted->setClipSource({});
        m_cast.scripted->setSpeech({});
        m_cast.scripted->setGestureTest({});
    }
    m_levelEffects.reset();
    attachScenes(nullptr, 0.0); // the scenes may outlive the stage they were hosted by
    m_playScenery.setLighting(nullptr);
    m_lights.reset();
    m_ai.reset(); // out of the player's step before he goes
    m_fighterMeshes.clear();
    m_targetMeshes.clear(); // they too hold the texture
    m_mesh.reset();         // before the dictionaries, whose texture it holds
    // The level's objects outlive the mode (they are gameplay's), its collision mesh and path data do not.
    if (m_objects != nullptr) {
        m_objects->world.bindCollision(nullptr);
        m_objects->world.paths = nullptr;
        m_objects->world.knock = nullptr;
    }
}

std::expected<void, Error> PlayLevelMode::spawnFighter(anim::Vec3 feet, float headingDegrees) {
    addFighter(feet, headingDegrees, {}, true);
    return {};
}

std::vector<std::string> PlayLevelMode::fighterModels() const {
    // The distinct models of the configured types a player can be drawn as, sorted.
    std::set<std::string> models;
    for (const characters::CharacterChoice& choice : characterChoices()) {
        models.insert(choice.model);
    }
    return {models.begin(), models.end()};
}

std::expected<void, Error> PlayLevelMode::spawnFighterAs(anim::Vec3 feet, float headingDegrees, std::string_view model,
                                                         ai::FighterSide side) {
    addFighter(feet, headingDegrees, std::string(model), true, side);
    return {};
}

void PlayLevelMode::clearFighters() {
    // Only the fighters the menus spawned, taken out as a level's script deletes a human: their entries stay (so the
    // fighters keep their indexes and the scripts' handles on the others stay good) and are no longer drawn. Their own
    // handles leave the scripts' hold first, as a deleted script human's do.
    unbindDebugFighters();
    m_ai->removeDebugSpawned();
}

std::string PlayLevelMode::fightersState() const {
    // The player's health, then each fighter's gang, health, top goal and queued actions.
    std::string line = std::format("player health {}", m_player->human().health().value());
    const ai::Brains& brains = m_ai->brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const ai::Brain& brain = brains.at(i);
        if (brain.type() == ai::BrainType::Player) {
            continue;
        }
        const ai::Goal* top = brain.reactionGoal() != nullptr ? brain.reactionGoal() : brain.topGoal();
        line += std::format("; {} health {} goal {:#x} actions {}",
                            brain.gang() != nullptr ? brain.gang()->name() : std::string("no gang"),
                            brain.human().health().value(), top != nullptr ? static_cast<int>(top->type()) : 0,
                            brain.actionCount());
    }
    return line;
}

WorldView PlayLevelMode::view(const human::PlayerSnapshot& snapshot, float drawDistance) const {
    // The current camera, in RenderWare's axes, kept upright by its up (a scripted camera's roll tips it,
    // docs/research/camera.md#locked-cameras).
    const world::Vec3 position = toRenderWare(snapshot.cameraEye);
    const anim::Vec3 look = anim::subtract(snapshot.cameraTarget, snapshot.cameraEye);
    const anim::Vec3 forwardGame = anim::length(look) > 1e-6F ? anim::normalise(look) : anim::Vec3{0.0F, 1.0F, 0.0F};
    const anim::Vec3 forward = directionToRenderWare(forwardGame);
    const anim::Vec3 upHint = directionToRenderWare(snapshot.cameraUp);
    anim::Vec3 right = anim::cross(forward, anim::length(upHint) > 1e-6F ? upHint : anim::Vec3{0.0F, 1.0F, 0.0F});
    right = anim::length(right) > 1e-6F ? anim::normalise(right) : anim::Vec3{-1.0F, 0.0F, 0.0F};
    const anim::Vec3 up = anim::cross(right, forward);
    // Through the current camera's lens (a locked camera's is narrower), its far clip capping the draw distance
    // (docs/research/world.md#a-frame).
    const camera::CameraLens lens{
        .fieldOfView = snapshot.fieldOfView, .nearClip = snapshot.nearClip, .farClip = snapshot.farClip};
    return viewFrom(world::CameraPose{.position = position,
                                      .forward = world::Vec3{forward.x, forward.y, forward.z},
                                      .up = world::Vec3{up.x, up.y, up.z},
                                      .right = world::Vec3{right.x, right.y, right.z}},
                    std::min(drawDistance, snapshot.farClip), lens);
}

WorldView PlayLevelMode::viewFrom(const world::CameraPose& pose, float drawDistance,
                                  const camera::CameraLens& lens) const {
    // A window of another shape keeps the view's height (as the world viewer does).
    const camera::ViewWindow window = camera::viewWindow(lens);
    const float aspect = m_engine.viewAspect();
    return WorldView{.pose = pose,
                     .halfWidth = window.halfHeight * aspect,
                     .halfHeight = window.halfHeight,
                     .nearClip = lens.nearClip,
                     .drawDistance = drawDistance};
}

WorldView PlayLevelMode::chosenView(const human::PlayerSnapshot& snapshot, float drawDistance, float alpha,
                                    const world::CameraPose* free) const {
    // `--camera` wins over every camera the game would show, with the current camera's clips.
    if (m_pinnedCamera) {
        const CameraPin& pin = *m_pinnedCamera;
        const camera::CameraView pinned =
            camera::pinnedView(anim::Vec3{pin.x, pin.y, pin.z}, anim::Quat{pin.qx, pin.qy, pin.qz, pin.qw},
                               pin.fieldOfView, snapshot.nearClip, snapshot.farClip);
        return worldViewOf(pinned, m_engine.viewAspect(), std::min(drawDistance, snapshot.farClip));
    }
    if (const std::optional<WorldView> scene = m_stage->cameraView(alpha, m_engine.viewAspect()); scene) {
        return *scene;
    }
    if (free != nullptr) {
        return viewFrom(*free, drawDistance);
    }
    return view(snapshot, drawDistance);
}

WorldView PlayLevelMode::stepSceneryView(const FrameTime& frame) {
    // The scenery's step round the newest step's camera (for a level: one streaming decision and the draw distance),
    // then that camera at the new draw distance: the next step's streaming reads the visibility pass made from it, so
    // it belongs to the simulation, not to the blended render.
    const std::optional<world::CameraPose> free =
        m_freeCamera ? std::optional<world::CameraPose>(m_freeCamera->current().pose()) : std::nullopt;
    const world::CameraPose* freePose = free ? &*free : nullptr;
    const world::Vec3 eye = chosenView(m_player->current(), m_drawDistance.current(), 1.0F, freePose).pose.position;
    m_playScenery.step(eye, frame);
    m_drawDistance.current() = m_playScenery.drawDistance();
    return chosenView(m_player->current(), m_drawDistance.current(), 1.0F, freePose);
}

void PlayLevelMode::stepFrozen(const FrameTime& frame) {
    // Only what keeps the picture whole as the camera's surroundings stream in: the scenery and the world objects
    // round the camera, with no time passing for them.
    m_drawDistance.commit();
    m_stepView = stepSceneryView(frame);
    m_playScenery.findVisible(m_stepView);
    stepWorldObjects(StepView{.pose = m_stepView.pose, .drawDistance = m_drawDistance.current()}, 0);
    recordDrawnPoses();
    ++m_stats.frames;
}

void PlayLevelMode::sceneOnly() {
    m_sceneOnly = true;
    m_playScenery.sceneOnly();
}

void PlayLevelMode::pinCamera(const CameraPin& pin) {
    m_pinnedCamera = pin;
    m_print(std::format("camera: pinned at ({:.3f}, {:.3f}, {:.3f}) quaternion ({:.4f}, {:.4f}, {:.4f}, {:.4f}) "
                        "fov {:.1f}\n",
                        pin.x, pin.y, pin.z, pin.qx, pin.qy, pin.qz, pin.qw, pin.fieldOfView));
}

std::expected<void, Error> PlayLevelMode::traceTo(const std::string& path) {
    // Text written as is (no newline translation), so a trace reads the same on every platform.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCode::Io, std::format("cannot write the trace {}", path));
    }
    file << human::traceHeader();
    m_trace = std::move(file);
    m_traceSteps = 0;
    return {};
}

void PlayLevelMode::enter() { m_playScenery.preload(toRenderWare(m_player->camera().position())); }

void PlayLevelMode::skin(const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet,
                         float heading, float lean, CharacterMesh& mesh, float scale) {
    // The pose's skinning matrices in the character's space (game axes, the feet at the origin, facing +y), then
    // leaned, turned, moved to the feet and into RenderWare's axes by the placement.
    const characters::CharacterModel& model = character.assets().model;
    const auto bones = anim::boneTransforms(character.skeleton(), pose);
    std::vector<anim::Mat34> matrices = characters::skinningMatrices(model, bones);
    // A scaled body (the Entity inspector's) grows about the feet, in the character's space: before the placement, so
    // the feet stay where they are, and the shader's normalised normal keeps the lighting.
    if (scale != 1.0F) {
        for (anim::Mat34& matrix : matrices) {
            matrix = anim::Mat34{.x = anim::scale(matrix.x, scale),
                                 .y = anim::scale(matrix.y, scale),
                                 .z = anim::scale(matrix.z, scale),
                                 .t = anim::scale(matrix.t, scale)};
        }
    }
    mesh.pose(matrices, characterPlacement(feet, heading, lean));
}

void PlayLevelMode::drawCharacter() const {
    CONEY_PERF_ZONE("render.humans");
    const DrawKindScope kind(DrawKind::Humans);
    draw::setRenderState(rw::ZTESTENABLE, 1);
    draw::setRenderState(rw::ZWRITEENABLE, 1);
    draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
    drawHumanPasses(*m_mesh, true, bloodTextureFor(m_player->human().health()));
    for (const Target& target : m_targets) {
        drawHumanPasses(*m_targetMeshes.at(target.human.get()), false, bloodTextureFor(target.human->health()));
    }
    for (const ai::AiHuman& fighter : m_ai->humans()) {
        const auto mesh = m_fighterMeshes.find(fighter.human.get());
        if (mesh != m_fighterMeshes.end() && !mesh->second.hidden) {
            drawHumanPasses(*mesh->second.mesh, false, bloodTextureFor(fighter.human->health()));
        }
    }
    // A scene's puppets are lit like any human (a bare render of their atomics crashes: issue 446); no blood layer.
    m_stage->drawPuppets([this](CharacterMesh& mesh) { drawHumanPasses(mesh, false, nullptr); });
}

void PlayLevelMode::drawHumanPasses(CharacterMesh& mesh, bool player, rw::Texture* blood) const {
    // The mark is around the original's draw, not in it: the layer it names is drawn only shaded by the mesh views,
    // and its selection adds no row to the lighting frame when it goes through the rig.
    SceneLighting& scene = m_lights->scene();
    scene.setMeshLayer(mesh.bloodAtomic());
    m_lights->drawHumanPasses(mesh, player, blood);
    scene.setMeshLayer(nullptr);
}

rw::Texture* PlayLevelMode::bloodTextureFor(const combat::Health& health) const {
    if (!m_blood) {
        return nullptr;
    }
    const graphics::BloodLayer layer = graphics::bloodLayerFor(health.fraction() * 100.0F);
    return layer.shows ? m_blood->texture(layer.texture) : nullptr;
}

bool PlayLevelMode::stepFrozenWorld(const FrameTime& frame) {
    // `--freeze-world`, once its first step is done: nothing advances.
    if (!worldFrozen()) {
        return false;
    }
    stepFrozen(frame);
    return true;
}

void PlayLevelMode::traceStep() {
    if (m_trace) {
        *m_trace << human::traceLine(++m_traceSteps, *m_player);
    }
    // Every human's row for this step, after all their updates.
    if (m_humansTrace) {
        m_humansTrace->write(m_ai->brains());
    }
    // Network play's view of the same humans: the host's snapshots and the state digest.
    if (m_netHumans != nullptr) {
        m_netHumans->capture(m_ai->brains());
    }
}

PlayLevelSimulation::StepView PlayLevelMode::stepScenery(const FrameTime& frame) {
    // The draw distance render() blends moves on a step. The scenery's step around the camera, then its visibility pass
    // from the newest step's camera. The pinned camera, a scene camera while one is current, or the free camera while
    // it is on, takes the follow camera's place.
    m_drawDistance.commit();
    m_stepView = stepSceneryView(frame);
    // The lights' flicker and the player's shadow dimming, stepped with the simulation.
    m_lights->step(m_stepView, m_scenery->collision(), m_player->human().position(),
                   static_cast<std::uint32_t>(std::lround(frame.seconds * 1000.0)));
    m_playScenery.findVisible(m_stepView);
    return StepView{.pose = m_stepView.pose, .drawDistance = m_drawDistance.current()};
}

void PlayLevelMode::hudStepped() {
    // Network play's view of each player's HUD, as this step's HUD was given it, and the HUD's standing state after it;
    // built only while it captures (a session, `--digest` or `--net-replay`).
    if (m_netHumans != nullptr && m_netHumans->capturing()) {
        const hud::HudStanding standing = hudStanding();
        m_netHumans->captureHuds(playerHuds(), &standing);
    }
    m_hudLayer->render();
    recordIconPoses();
}

void PlayLevelMode::stepEnded(const FrameTime& frame) {
    // `--freeze-world`: the world stops here, its animations held at this step's time.
    if (m_freezeWorld && !m_frozenSince) {
        m_frozenSince = frame.gameTicks;
        m_playScenery.freezeAnimation(millisecondsOf(frame.gameTicks));
        m_print(std::format("world: frozen after frame {}\n", frame.index));
    }
}

void PlayLevelMode::render(const RenderTime& time) {
    // Everything drawn comes from the player's snapshots and the draw distance, `alpha` of the way from the step before
    // to the newest one; in a frozen world exactly the newest, at the time it froze (the scenery's fade-in excepted).
    const bool frozen = worldFrozen();
    const float alpha = frozen ? 1.0F : time.alpha;
    const std::uint64_t nowMs = millisecondsOf(frozen ? m_frozenSince.value_or(time.gameTicks) : time.gameTicks);
    human::PlayerSnapshot snapshot = human::interpolate(m_player->previous(), m_player->current(), alpha);
    const float drawDistance = lerp(m_drawDistance.previous(), m_drawDistance.current(), alpha);
    // The pinned camera, the scene camera while one is current, or the free camera; and player 1 as a scene poses him
    // while it holds him.
    const std::optional<world::CameraPose> free =
        m_freeCamera ? std::optional<world::CameraPose>(blendedFreeCamera(*m_freeCamera, alpha).pose()) : std::nullopt;
    const WorldView blended = chosenView(snapshot, drawDistance, alpha, free ? &*free : nullptr);
    if (const std::optional<scenes::RoleFrame> posed =
            sceneHoldsPlayer() ? m_stage->frameOf(m_playerHandle, alpha) : std::nullopt) {
        snapshot.pose = posed->pose;
        snapshot.feet = posed->feet;
        snapshot.heading = posed->heading;
        snapshot.lean = 0.0F;
    }
    // Where each human is drawn this frame, by the health rings' id of it (its address).
    std::map<std::uint64_t, anim::Vec3> ringFeet;
    const auto ringKey = [](const void* human) {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(human));
    };
    if (m_engine.drawsPixels()) {
        CONEY_PERF_ZONE("render.skin");
        m_stage->skinPuppets(alpha, [](const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet,
                                       float heading,
                                       CharacterMesh& mesh) { skin(character, pose, feet, heading, 0.0F, mesh); });
        skin(playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading, snapshot.lean, *m_mesh,
             m_player->human().meshScale());
        m_attachedDraws.clear();
        poseHat(m_player->human(), playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading);
        poseHeldObject(m_player->human(), playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading);
        poseCarriedProp(m_player->human(), playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading);
        const raycast::CollisionMesh& ground = m_scenery->collision();
        m_lights->addShadow(ground, snapshot.feet);
        ringFeet[ringKey(&m_player->human())] = snapshot.feet;
        for (Target& target : m_targets) {
            const human::TargetSnapshot pose =
                human::interpolate(target.human->previous(), target.human->current(), alpha);
            // As the scene's character, or the one the Entity inspector's Model edit gave it.
            skin(target.character != nullptr ? *target.character : *m_character, pose.pose, pose.feet, pose.heading,
                 0.0F, *m_targetMeshes.at(target.human.get()));
            m_lights->addShadow(ground, pose.feet);
            ringFeet[ringKey(target.human.get())] = pose.feet;
        }
        const std::vector<ai::AiHuman>& fighters = m_ai->humans();
        for (std::size_t i = 0; i < fighters.size(); ++i) {
            const auto found = m_fighterMeshes.find(fighters[i].human.get());
            if (found == m_fighterMeshes.end()) {
                continue;
            }
            // A deleted human is not drawn.
            if (fighters[i].removed) {
                found->second.hidden = true;
                continue;
            }
            human::TargetSnapshot pose = human::interpolate(fighters[i].previous, fighters[i].current, alpha);
            // A cast human a scene holds is drawn as the scene poses it.
            if (const double handle = m_scenes != nullptr ? castHandleOf(fighters[i]) : 0.0;
                handle != 0.0 && m_stage->holds(handle)) {
                if (const std::optional<scenes::RoleFrame> posed = m_stage->frameOf(handle, alpha)) {
                    pose.pose = posed->pose;
                    pose.feet = posed->feet;
                    pose.heading = posed->heading;
                }
            }
            const human::PlayerCharacter& character = characterOf(*fighters[i].human);
            skin(character, pose.pose, pose.feet, pose.heading, 0.0F, *found->second.mesh,
                 fighters[i].human->meshScale());
            poseHat(*fighters[i].human, character, pose.pose, pose.feet, pose.heading);
            // What he holds, posed by his own hand as player 1's is (docs/research/objects.md#given-weapon).
            poseHeldObject(*fighters[i].human, character, pose.pose, pose.feet, pose.heading);
            poseCarriedProp(*fighters[i].human, character, pose.pose, pose.feet, pose.heading);
            m_lights->addShadow(ground, pose.feet);
            ringFeet[ringKey(fighters[i].human.get())] = pose.feet;
        }
    }
    // Over the frame, before its present: the level's screen effects, the HUD's sprites of the newest step, then the
    // scene's letterbox and fade. The scenery draws itself through the blended view, with the character and the debug
    // lines among its objects.
    if (m_levelEffects) {
        const bool cardHeld = m_scenes != nullptr && m_scenes->holdingObject();
        m_engine.addFrameOverlay(
            [this, cardHeld](RenderEngine& engine) { m_levelEffects->drawOverlay(engine, cardHeld); });
    }
    // The blur pulse, then the level's screen tint. They cover the HUD, as the original's screen effects follow it,
    // except while a blur pulse runs: then they go first and the HUD stays sharp over them
    // (docs/research/rendering.md#tint).
    const bool effectsFirst = m_levelEffects && m_levelEffects->screenEffectsFirst();
    if (effectsFirst) {
        m_engine.addFrameOverlay([this](RenderEngine& engine) { m_levelEffects->drawScreenEffects(engine); });
    }
    if (!m_hudHidden) {
        m_engine.addFrameOverlay([this](RenderEngine& engine) { m_hudLayer->draw(engine); });
    }
    if (m_levelEffects && !effectsFirst) {
        m_engine.addFrameOverlay([this](RenderEngine& engine) { m_levelEffects->drawScreenEffects(engine); });
    }
    m_engine.addFrameOverlay([this, nowMs](RenderEngine& engine) { m_stage->drawOverlay(engine, nowMs); });
    // A layer over all of them (the pause menu), added last so it draws last.
    if (m_overlay != nullptr) {
        m_engine.addFrameOverlay([overlay = *m_overlay](RenderEngine& engine) { overlay(engine); });
    }
    // The cars take the objects' lights at squared distance 0, each atomic for its own sphere (**Coney's stand-in**,
    // #657: the original selects them once for the clump's sphere, CarInstance_Render 0x00172c70).
    const auto drawCars = [this](graphics::CarPass pass) {
        if (m_levelEffects) {
            m_levelEffects->drawCars([this](const ObjectDraw& draw) { drawObject(m_engine, m_lights->scene(), draw); },
                                     pass);
        }
    };
    // The World page's parts (Coney's own debug tooling, docs/guides/debug-menu.md#world): all on without the debug
    // tools' switches, so the frame below draws as the original's order has it.
    const graphics::WorldParts parts =
        m_lightingSwitches != nullptr ? m_lightingSwitches->parts : graphics::WorldParts{};
    // Between the `s` and the `d` world (step 7): the humans, the cars' opaque parts, the world objects.
    const auto drawObjects = [this, &drawCars, &parts, alpha] {
        // `--scene-only` leaves the humans out of the frame description, whatever the World page says.
        if (parts.humans && !m_sceneOnly) {
            drawCharacter();
        }
        if (!parts.objects) {
            return;
        }
        const DrawKindScope kind(DrawKind::Objects);
        drawCars(graphics::CarPass::Opaque);
        drawWorldObjects(alpha);
    };
    // After the `d` world, as the world pass orders it (docs/research/world.md#a-frame, steps 9-10): the water, then
    // the see-through panes and the cars' glass, then the effects; then, as the sprite batches after the whole world
    // (graphics.md#a-frame), the blob shadows, and the health rings over them. **Coney's choice**: the coronas, which
    // the original batches between the shadows and the rings, follow the rings (WorldRenderer::render draws them).
    const auto afterWorlds = [this, &drawCars, &snapshot, &blended, &ringFeet, &parts, alpha] {
        // `--scene-only`: of what follows the worlds, only the cars' glass, which goes into the frame description.
        if (m_sceneOnly) {
            if (parts.objects) {
                const DrawKindScope kind(DrawKind::Objects);
                drawCars(graphics::CarPass::Glass);
            }
            return;
        }
        std::optional<DrawKindScope> kind(std::in_place, DrawKind::Effects);
        if (m_levelEffects && parts.water) {
            m_levelEffects->drawWater();
        }
        kind.emplace(DrawKind::Objects);
        if (parts.glass) {
            drawGlass();
        }
        if (parts.objects) {
            drawCars(graphics::CarPass::Glass);
        }
        kind.emplace(DrawKind::Effects);
        // The throw-aim arc, blended by its vertices' alpha among the see-through things (**Coney's stand-in** for its
        // place in WorldManager_Render's order, not traced).
        if (parts.effects) {
            drawThrowArc(alpha);
            if (m_levelEffects) {
                m_levelEffects->drawInScene(blended.pose);
                m_levelEffects->drawGlints(m_glints.sprites(), blended.pose);
            }
        }
        {
            CONEY_PERF_ZONE("render.shadows_rings");
            m_lights->drawShadows();
            if (parts.rings) {
                drawRings(ringFeet);
            }
        }
        kind.emplace(DrawKind::Other);
        drawSceneLines(snapshot, alpha);
    };
    CONEY_PERF_ZONE("render.scene");
    // The debug tools' mesh views, when set and on this frame, see every model drawn (MeshViewHook); otherwise the
    // rig draws each model as it always does. The hook is set for this frame only: a frame it is off leaves none, so
    // what an earlier frame set cannot reach this one. The NULL renderer asks too (its views draw nothing there:
    // they leave the GL calls out themselves), so the tests see the hook wired.
    MeshViewHook* views = m_meshViews && m_meshViews->beginFrame(*this) ? m_meshViews.get() : nullptr;
    m_lights->scene().setMeshViews(views);
    // The lighting lab's switches, and the game camera's view for its Light from while the free camera flies (the
    // original culls the lights from the player's camera).
    m_lights->scene().setSwitches(m_lightingSwitches);
    std::optional<graphics::LightView> gameView;
    if (m_lightingSwitches != nullptr && m_freeCamera) {
        const WorldView game = chosenView(snapshot, drawDistance, alpha, nullptr);
        gameView = graphics::LightView{.position = game.pose.position,
                                       .forward = game.pose.forward,
                                       .nearClip = game.nearClip,
                                       .farClip = game.drawDistance};
    }
    m_lights->scene().setGameView(gameView);
    {
        const DrawKindScope kind(DrawKind::Scenery);
        m_playScenery.draw(m_engine, blended, millisecondsOf(time.gameTicks), drawObjects, afterWorlds);
    }
    m_lights->scene().setMeshViews(nullptr);
}

void PlayLevelMode::renderWithOverlay(const RenderTime& time,
                                      const std::function<void(graphics::RenderDevice&)>& overlay) {
    m_overlay = &overlay;
    render(time);
    m_overlay = nullptr;
}

void PlayLevelMode::renderWorldWithOverlay(const RenderTime& time,
                                           const std::function<void(graphics::RenderDevice&)>& overlay) {
    m_hudHidden = true;
    renderWithOverlay(time, overlay);
    m_hudHidden = false;
}

world::DebugCamera PlayLevelMode::blendedFreeCamera(const Interpolated<world::DebugCamera>& camera, float alpha) {
    // As the sandbox viewer blends its camera: the position, and the turn the short way round.
    const world::DebugCamera& from = camera.previous();
    const world::DebugCamera& to = camera.current();
    world::DebugCamera between(world::Vec3{lerp(from.position().x, to.position().x, alpha),
                                           lerp(from.position().y, to.position().y, alpha),
                                           lerp(from.position().z, to.position().z, alpha)});
    between.setOrientation(lerpAngle(from.yaw(), to.yaw(), alpha), lerp(from.pitch(), to.pitch(), alpha));
    return between;
}

void PlayLevelMode::drawSceneLines(const human::PlayerSnapshot& snapshot, float alpha) const {
    if (!m_sceneLines || !m_engine.drawsPixels()) {
        return;
    }
    LineList lines;
    for (const SceneLine& line : m_sceneLines(*this, snapshot, alpha)) {
        lines.add(line.a, line.b, line.colour);
    }
    lines.draw();
}

anim::Vec3 PlayLevelMode::playerFeet() const { return m_player->human().position(); }

float PlayLevelMode::playerHeadingDegrees() const {
    return m_player->human().heading() * 180.0F / std::numbers::pi_v<float>;
}

float PlayLevelMode::playerSpeed() const { return m_player->human().speed(); }

std::string PlayLevelMode::playerState() const {
    const human::Human& human = m_player->human();
    return std::format("{}, clip {}, {}, {}, stamina {}/{}{}", gaitName(human.gait()), human.animator().animId(),
                       human.flying()     ? "noclip"
                       : human.airborne() ? "airborne"
                                          : "grounded",
                       human::traversalName(human.traversal()), human.stamina().value(), human.stamina().maximum(),
                       human.sprinting() ? ", sprint" : "");
}

void PlayLevelMode::teleport(const human::Place& place) {
    // Dropped onto the ground below the spot, as a start is; left where it is over nothing.
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    raycast::Vec3 point{place.feet.x, place.feet.y, place.feet.z + kTeleportDrop};
    if (!raycast::dropToGround(mesh, kTeleportDrop * 2.0F, point)) {
        point.z = place.feet.z;
    }
    m_player->standAt(&mesh, human::PlayerStart{.position = anim::Vec3{point.x, point.y, point.z},
                                                .headingDegrees = place.headingDegrees});
}

void PlayLevelMode::startAt(const StartPlace& place) {
    // Dropped onto the ground below the spot, as the debug menus' teleport is; then the camera, when given.
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    raycast::Vec3 point{place.x, place.y, place.z + kTeleportDrop};
    if (!raycast::dropToGround(mesh, kTeleportDrop * 2.0F, point)) {
        point.z = place.z;
    }
    m_player->standAt(&mesh, human::PlayerStart{.position = anim::Vec3{point.x, point.y, point.z},
                                                .headingDegrees = place.headingDegrees});
    if (place.cameraDistance.has_value() && place.cameraYawDegrees.has_value()) {
        m_player->placeCamera(*place.cameraDistance, *place.cameraYawDegrees * std::numbers::pi_v<float> / 180.0F);
    }
}

void PlayLevelMode::teleportPlayer(const world_objects::Placement& placement, world_objects::TeleportMotion motion) {
    // The placement drops him onto what is at most 0.5 m below the point, unless a scene holds him; TeleportToFlag
    // then stops his move (Object_TeleportToFlag), Teleport leaves it (Object_Teleport).
    const std::array<float, 3>& p = placement.position;
    const human::PlayerStart start{.position = anim::Vec3{p[0], p[1], p[2]},
                                   .headingDegrees = placement.headingDegrees};
    if (motion == world_objects::TeleportMotion::Stopped) {
        m_player->teleport(placeGround(m_playerHandle), start);
    } else {
        m_player->place(placeGround(m_playerHandle), start);
    }
    brainPlaced(m_player->human());
}

void PlayLevelMode::brainPlaced(const human::Human& human) {
    if (m_ai == nullptr) {
        return;
    }
    if (ai::Brain* brain = m_ai->brainOf(human); brain != nullptr) {
        brain->placed();
    }
}

const raycast::CollisionMesh* PlayLevelMode::placeGround(double human) const {
    // Human_SetTransform drops no human a scene holds (human +0x280 not -1).
    if (m_scenery == nullptr || (m_scenes != nullptr && m_stage->holds(human))) {
        return nullptr;
    }
    return &m_scenery->collision();
}

bool PlayLevelMode::startTagSpray(const std::array<float, 3>& point) {
    return m_player->human().startTagSpray(anim::Vec3{point[0], point[1], point[2]});
}

std::string PlayLevelMode::heldObjectType() const {
    const human::ScriptState& script = m_player->human().script();
    return script.heldObject != world_objects::kNoObject ? script.heldObjectName : std::string();
}

void PlayLevelMode::dropHeldObject() {
    // Out of the hand at once, with no clip: it falls from where the hand holds it.
    human::Human& human = m_player->human();
    human::ScriptState& script = human.script();
    if (script.heldObject == world_objects::kNoObject) {
        return;
    }
    const double held = std::exchange(script.heldObject, world_objects::kNoObject);
    script.heldObjectName.clear();
    dropHeld(human, held);
}

bool PlayLevelMode::tagSprayLooping() const { return m_player->human().tagSprayLooping(); }

bool PlayLevelMode::tagSprayPlaying() const { return m_player->human().tagSprayPlaying(); }

void PlayLevelMode::restartTagSpray() { m_player->human().restartTagSpray(); }

void PlayLevelMode::endTagSpray() { m_player->human().endTagSpray(); }

anim::Vec3 PlayLevelMode::cameraEye() const { return m_player->current().cameraEye; }

anim::Vec3 PlayLevelMode::cameraTarget() const { return m_player->current().cameraTarget; }

void PlayLevelMode::setFreeCamera(bool on) {
    if (!on) {
        m_freeCamera.reset();
        return;
    }
    if (m_freeCamera) {
        return;
    }
    // At the follow camera's eye, looking where it looks (RenderWare's axes: y up, the heading 0 along +z).
    world::DebugCamera camera(toRenderWare(m_player->camera().position()), kFreeCameraSpeed);
    const anim::Vec3 look = directionToRenderWare(m_player->camera().forward());
    camera.setOrientation(std::atan2(look.x, look.z), std::asin(std::clamp(look.y, -1.0F, 1.0F)));
    m_freeCamera.emplace(camera);
}

std::string PlayLevelMode::summary() const {
    const human::Human& human = m_player->human();
    const anim::Vec3 p = human.position();
    const anim::Vec3 c = m_player->camera().position();
    // The fight's counts, with fighters or targets to fight.
    std::string fight;
    if (m_ai->count() > 0) {
        int damage = 0;
        int blocked = 0;
        int reactions = 0;
        for (const ai::AiHuman& entry : m_ai->humans()) {
            const human::Fighter& fighter = entry.human->fighter();
            damage += fighter.damageDealt();
            blocked += fighter.hitsBlocked() + fighter.hitsDucked();
            reactions += fighter.victim().reactions();
        }
        fight +=
            std::format("; fighters {}: damage {} blocked {} reactions {}, player hits {} health {}", m_ai->count(),
                        damage, blocked, reactions, human.fighter().hitsLanded(), human.health().value());
    }
    if (!m_targets.empty()) {
        const human::Fighter& fighter = human.fighter();
        int health = 0;
        int reactions = 0;
        int stuns = 0;
        int knockdowns = 0;
        for (const Target& target : m_targets) {
            health += target.human->health().value();
            reactions += target.human->reactions();
            stuns += target.human->stuns();
            knockdowns += target.human->knockdowns();
        }
        fight += std::format("; fight: hits {} damage {} power {} rage {}, targets {} health {} reactions {} stuns {} "
                             "knockdowns {}",
                             fighter.hitsLanded(), fighter.damageDealt(), fighter.combat().power().value(),
                             fighter.combat().rage().value(), m_targets.size(), health, reactions, stuns, knockdowns);
    }
    return std::format(
        "play: {} frames, player at ({:.2f}, {:.2f}, {:.2f}) heading {:.1f} speed {:.2f} gait {} clip {} "
        "{} {} stamina {}, travelled {:.2f} m, respawns {}; camera {:.2f} m away{}{}{}\n",
        m_stats.frames, p.x, p.y, p.z, human.heading() * 180.0F / std::numbers::pi_v<float>, human.speed(),
        gaitName(human.gait()), human.animator().animId(), human.airborne() ? "airborne" : "grounded",
        human::traversalName(human.traversal()), human.stamina().value(), m_stats.travelled, m_player->respawns(),
        anim::distance(c, m_player->camera().lookAt()), fight, m_playScenery.summary(),
        m_scenes != nullptr ? m_stage->summary() + scenesSummary(m_scenes->stats()) : std::string{});
}

} // namespace coney::platform
