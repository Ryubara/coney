// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_level_mode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include <rw.h>

#include "animation/anim_pose.h"
#include "camera/camera_lens.h"
#include "camera/camera_view.h"
#include "camera/cameras.h"
#include "characters/character_data.h"
#include "characters/character_rig.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "fileio/executable.h"
#include "gamemodes/game_mode_stack.h"
#include "graphics/human_blood.h"
#include "human/human_animator.h"
#include "human/player_trace.h"
#include "platform/play_level_effects.h"
#include "raycast/collision_mesh.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "warriors/inventory.h"

namespace coney::platform {

namespace {

// Game time in milliseconds from GameTimer ticks.
std::uint64_t millisecondsOf(std::uint64_t ticks) { return ticks / (GameTimer::kTicksPerSecond / 1000); }

// How far above a teleport's spot the drop onto the ground starts, and how far below it reaches, in metres.
constexpr float kTeleportDrop = 2.0F;
// How fast the free camera flies, in metres a second: the sandbox viewer's walking pace for a level's scale.
constexpr float kFreeCameraSpeed = 8.0F;
// The most collision triangles the Debug draw wireframe draws in one frame.
constexpr std::size_t kMaxWireTriangles = 6000;
// The size of a debug marker's cross and of a heading line, in metres.
constexpr float kMarkerSize = 0.3F;
constexpr float kHeadingLength = 1.0F;
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
    // Adds a cross of three lines `size` long, centred on `p`.
    void cross(anim::Vec3 p, float size, graphics::Rgba colour) {
        const float h = size * 0.5F;
        add({p.x - h, p.y, p.z}, {p.x + h, p.y, p.z}, colour);
        add({p.x, p.y - h, p.z}, {p.x, p.y + h, p.z}, colour);
        add({p.x, p.y, p.z - h}, {p.x, p.y, p.z + h}, colour);
    }
    // Draws the lines through the current camera: tested against depth, not writing it, untextured.
    void draw() {
        if (m_vertices.empty()) {
            return;
        }
        rw::SetRenderState(rw::ZTESTENABLE, 1);
        rw::SetRenderState(rw::ZWRITEENABLE, 0);
        rw::SetRenderState(rw::VERTEXALPHA, 0);
        rw::SetRenderStatePtr(rw::TEXTURERASTER, nullptr);
        rw::im3d::Transform(m_vertices.data(), static_cast<rw::int32>(m_vertices.size()), nullptr,
                            rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA);
        rw::im3d::RenderPrimitive(rw::PRIMTYPELINELIST);
        rw::im3d::End();
        rw::SetRenderState(rw::ZWRITEENABLE, 1);
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

// The level number of a scene called `name` (99 for `level99`), which picks the Armies of the Night models; 0 for a
// sandbox or any other name.
int levelNumberOf(std::string_view name) {
    constexpr std::string_view kPrefix = "level";
    if (!name.starts_with(kPrefix) || name.size() == kPrefix.size()) {
        return 0;
    }
    int number = 0;
    for (const char c : name.substr(kPrefix.size())) {
        if (c < '0' || c > '9' || number > 9999) {
            return 0;
        }
        number = (number * 10) + (c - '0');
    }
    return number;
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
PlayLevelMode::createInSandbox(RenderEngine& engine, const io::Wad& wad, sandbox::SandboxWorld world,
                               const std::optional<std::string>& spawn, std::function<void(std::string_view)> print,
                               const PlayerSetup& setup) {
    auto scenery = SandboxPlayScenery::create(engine, std::move(world), spawn);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    PlayerSetup used = setup;
    used.snapToGround = true;
    return createWith(engine, wad, std::move(*scenery), std::move(print), used);
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
    // The level's cars, particles and motion blur, when gameplay brought them; the cars' boxes join the level's
    // collision before anything takes the mesh.
    std::unique_ptr<PlayLevelEffects> levelEffects;
    if (cast != nullptr && (cast->effects != nullptr || cast->cars != nullptr)) {
        levelEffects = std::make_unique<PlayLevelEffects>(engine, wad, cast->effects, cast->cars, print);
        if (auto added = scenery->addObstacles(levelEffects->carObstacles()); !added) {
            print(std::format("cars: no collision for the parked cars: {}\n", added.error().message));
        }
    }
    return std::unique_ptr<PlayLevelMode>(new PlayLevelMode(engine, wad, std::move(scenery), std::move(*loaded),
                                                            std::move(print), std::move(model), used, cast,
                                                            std::move(levelEffects)));
}

PlayLevelMode::PlayLevelMode(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
                             LoadedCharacter loaded, std::function<void(std::string_view)> print, std::string model,
                             const PlayerSetup& setup, const ScriptedCast* cast,
                             std::unique_ptr<PlayLevelEffects> levelEffects)
    : m_engine(engine), m_wad(wad), m_scenery(std::move(scenery)), m_character(std::move(loaded.character)),
      m_dictionaries(std::move(loaded.dictionaries)), m_types(setup.types), m_type(setup.type),
      m_levelNumber(levelNumberOf(m_scenery->name())),
      // A start with no snap is spawned without the mesh; the first update's ground snap then settles the feet.
      m_player(std::make_unique<human::Player>(*m_character, setup.snapToGround ? &m_scenery->collision() : nullptr,
                                               m_scenery->start(), human::playerClassOf(setup.types, setup.type))),
      m_print(std::move(print)), m_positions(m_character->assets().model.vertices.size()),
      m_normals(m_character->assets().model.vertices.size()), m_drawDistance(m_scenery->drawDistance()),
      m_model(std::move(model)) {
    m_texture = textureOf(m_dictionaries);
    // The blood of every human's second pass; without it humans are drawn in one pass.
    if (auto blood = BloodTextures::load(engine, wad); blood) {
        m_blood = std::move(*blood);
    } else {
        m_print(std::format("humans: no blood textures ({}); drawn without them\n", blood.error().message));
    }
    m_mesh = std::make_unique<CharacterMesh>(m_character->assets().model, m_texture);
    // The level's lights as its scripts set them, or a stand-in; the scenery draws with them too.
    m_lights = std::make_unique<PlayLighting>(engine, wad, cast != nullptr ? cast->lighting : nullptr,
                                              m_scenery->lightDirection(), m_print);
    m_scenery->setLighting(&m_lights->scene());
    m_print(m_lights->summary());
    makeTargets(m_texture);
    // The glints' blink draws from the game's random table when the disc's executable has the NTSC-U one.
    if (auto table = io::readExecutableWords(wad.disc(), GameRandom::kExecutableName, GameRandom::kTableAddress,
                                             GameRandom::kTableSize)) {
        m_glints.setTable(*table);
    }
    // The brains plan their moves on the level's routes, when it has path data.
    if (const world::PathMap* paths = m_scenery->pathMap(); paths != nullptr) {
        m_planner = std::make_unique<ai::RoutePlanner>(*paths);
    }
    if (cast != nullptr) {
        bindObjects(cast->objects, cast->recorded);
        m_cars = cast->cars;
        bindPickups(cast->pickups);
        m_sound = cast->sound;
        makeWorldObjects(*cast);
    }
    // The AI humans in the player's step: the level's scripts' humans, or the layout's fighters.
    if (cast != nullptr && cast->brains != nullptr && cast->scripted != nullptr) {
        makeCast(*cast, setup.ai);
    } else {
        m_ai = std::make_unique<ai::AiHumans>(*m_player, *m_character, setup.ai);
        m_ai->brains().setPlanner(m_planner.get());
        m_ai->brains().setCollision(&m_scenery->collision());
        for (const sandbox::FighterPoint& point : m_scenery->fighters()) {
            addFighter(point.position, point.headingDegrees);
        }
        if (m_ai->count() > 0) {
            m_print(std::format("fighters: {} from the layout ({} configuration calls read)\n", m_ai->count(),
                                m_ai->config().callsRead));
        }
    }
    // The HUD, with the player on panel 0 (docs/research/hud.md#the-player-panel).
    m_hud = HudLayer::create(wad, engine.drawsPixels(), m_print);
    m_hud->hud().attachPlayer(0, m_type);
    makeStage();
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

void PlayLevelMode::useHud(hud::Hud& shared) {
    m_hud->useHud(shared);
    shared.attachPlayer(0, m_type);
}

PlayLevelMode::~PlayLevelMode() {
    // The dynamic clips go with the mode; the scripts' hold (gameplay's, which outlives it) must not use them.
    if (m_cast.scripted != nullptr) {
        m_cast.scripted->setClipSource({});
        m_cast.scripted->setSpeech({});
    }
    m_levelEffects.reset();
    attachScenes(nullptr, 0.0); // the scenes may outlive the stage they were hosted by
    m_scenery->setLighting(nullptr);
    m_lights.reset();
    m_ai.reset(); // out of the player's step before he goes
    m_fighterMeshes.clear();
    m_targets.clear(); // their meshes too hold the texture
    m_mesh.reset();    // before the dictionaries, whose texture it holds
    // The level's objects outlive the mode (they are gameplay's), its collision mesh and path data do not.
    if (m_objects != nullptr) {
        m_objects->world.collision = nullptr;
        m_objects->world.paths = nullptr;
    }
}

void PlayLevelMode::makeTargets(rw::Texture* texture) {
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    const std::size_t vertices = m_character->assets().model.vertices.size();
    std::uint32_t seed = 1;
    for (const sandbox::TargetPoint& point : m_scenery->targets()) {
        // Dropped onto the ground below its spot, as a teleport is.
        raycast::Vec3 feet{point.position.x, point.position.y, point.position.z + kTeleportDrop};
        if (!raycast::dropToGround(mesh, kTeleportDrop * 2.0F, feet)) {
            feet.z = point.position.z;
        }
        // **Coney's choice**: a target looks like the player (the one character Coney loads); its own model waits
        // for the character classes.
        Target target;
        target.human = std::make_unique<human::TargetHuman>(
            m_character->anims(), human::AnimSlots::player(), anim::referenceRotations(), point.health,
            anim::Vec3{feet.x, feet.y, feet.z}, point.headingDegrees * std::numbers::pi_v<float> / 180.0F, seed++);
        target.mesh = std::make_unique<CharacterMesh>(m_character->assets().model, texture);
        target.positions.resize(vertices);
        target.normals.resize(vertices);
        m_targetPointers.push_back(target.human.get());
        m_combatants.push_back(target.human.get());
        m_targets.push_back(std::move(target));
    }
    if (!m_targets.empty()) {
        m_print(std::format("targets: {} from the layout\n", m_targets.size()));
    }
}

void PlayLevelMode::addFighter(anim::Vec3 spot, float headingDegrees) {
    // Dropped onto the ground below its spot, as a teleport is.
    raycast::Vec3 feet{spot.x, spot.y, spot.z + kTeleportDrop};
    if (!raycast::dropToGround(m_scenery->collision(), kTeleportDrop * 2.0F, feet)) {
        feet.z = spot.z;
    }
    m_ai->spawnFighter(&m_scenery->collision(), anim::Vec3{feet.x, feet.y, feet.z}, headingDegrees);
    const std::size_t vertices = m_character->assets().model.vertices.size();
    FighterMesh mesh;
    mesh.character = m_character.get();
    mesh.mesh = std::make_unique<CharacterMesh>(m_character->assets().model, m_texture);
    mesh.positions.resize(vertices);
    mesh.normals.resize(vertices);
    m_fighterMeshes.push_back(std::move(mesh));
}

std::expected<void, Error> PlayLevelMode::spawnFighter(anim::Vec3 feet, float headingDegrees) {
    addFighter(feet, headingDegrees);
    return {};
}

void PlayLevelMode::clearFighters() {
    m_ai->clear();
    m_fighterMeshes.clear();
}

std::string PlayLevelMode::fightersState() const {
    // The player's health, then each fighter's health, top goal and queued actions.
    std::string line = std::format("player health {}", m_player->human().health().value());
    const ai::Brains& brains = m_ai->brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const ai::Brain& brain = brains.at(i);
        if (brain.type() == ai::BrainType::Player) {
            continue;
        }
        const ai::Goal* top = brain.reactionGoal() != nullptr ? brain.reactionGoal() : brain.topGoal();
        line += std::format("; health {} goal {:#x} actions {}", brain.human().health().value(),
                            top != nullptr ? static_cast<int>(top->type()) : 0, brain.actionCount());
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
    m_scenery->step(eye, frame);
    m_drawDistance.current() = m_scenery->drawDistance();
    return chosenView(m_player->current(), m_drawDistance.current(), 1.0F, freePose);
}

void PlayLevelMode::stepFrozen(const FrameTime& frame) {
    // Only what keeps the picture whole as the camera's surroundings stream in: the scenery and the world objects
    // round the camera, with no time passing for them.
    m_drawDistance.commit();
    const WorldView stepView = stepSceneryView(frame);
    m_scenery->findVisible(stepView);
    stepWorldObjects(stepView.pose.position, stepView, 0);
    ++m_stats.frames;
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

void PlayLevelMode::enter() { m_scenery->preload(toRenderWare(m_player->camera().position())); }

void PlayLevelMode::skin(const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet,
                         float heading, float lean, std::vector<anim::Vec3>& positions,
                         std::vector<anim::Vec3>& normals) {
    // The pose, skinned in the character's space (game axes, the feet at the origin, facing +y).
    const characters::CharacterModel& model = character.assets().model;
    const auto bones = anim::boneTransforms(character.skeleton(), pose);
    const std::vector<anim::Mat34> matrices = characters::skinningMatrices(model, bones);
    characters::skinVertices(model, matrices, positions, normals);
    // Then leaned into the turn about the forward axis at the feet (**Coney's choice** of axis and pivot: the
    // research gives the lean's angle, not how the body takes it), turned by the heading about z, moved to the feet,
    // and into RenderWare's axes.
    const float lc = std::cos(-lean);
    const float ls = std::sin(-lean);
    const auto roll = [lc, ls](anim::Vec3 v) { return anim::Vec3{v.x * lc + v.z * ls, v.y, -v.x * ls + v.z * lc}; };
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const anim::Vec3 p = roll(positions[i]);
        const anim::Vec3 n = roll(normals[i]);
        const anim::Vec3 placed{p.x * c - p.y * s + feet.x, p.x * s + p.y * c + feet.y, p.z + feet.z};
        positions[i] = anim::Vec3{placed.x, placed.z, -placed.y};
        normals[i] = directionToRenderWare(anim::Vec3{n.x * c - n.y * s, n.x * s + n.y * c, n.z});
    }
}

void PlayLevelMode::drawCharacter() const {
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    m_lights->drawHumanPasses(*m_mesh, true, bloodTextureFor(m_player->human().health()));
    for (const Target& target : m_targets) {
        m_lights->drawHumanPasses(*target.mesh, false, bloodTextureFor(target.human->health()));
    }
    const std::vector<ai::AiHuman>& fighters = m_ai->humans();
    for (std::size_t i = 0; i < m_fighterMeshes.size(); ++i) {
        const FighterMesh& fighter = m_fighterMeshes[i];
        if (!fighter.hidden) {
            rw::Texture* blood = i < fighters.size() ? bloodTextureFor(fighters[i].human->health()) : nullptr;
            m_lights->drawHumanPasses(*fighter.mesh, false, blood);
        }
    }
    m_stage->drawPuppets();
    m_lights->drawShadows();
}

rw::Texture* PlayLevelMode::bloodTextureFor(const combat::Health& health) const {
    if (!m_blood) {
        return nullptr;
    }
    const graphics::BloodLayer layer = graphics::bloodLayerFor(health.fraction() * 100.0F);
    return layer.shows ? m_blood->texture(layer.texture) : nullptr;
}

ModeResult PlayLevelMode::update(GameModeStack& stack, const FrameTime& frame) {
    // `--freeze-world`, once its first step is done: nothing advances.
    if (worldFrozen()) {
        stepFrozen(frame);
        return ModeResult::Stay;
    }
    // The draw distance render() blends moves on a step.
    m_drawDistance.commit();

    // The free camera, when on, takes pad 1 and the player stands still, as a frozen one does.
    const Pad& pad = stack.pads().port(0);
    if (m_freeCamera) {
        m_freeCamera->commit();
        m_freeCamera->current().update(pad, static_cast<float>(frame.seconds));
    }
    static const Pad kStill;
    const bool picking = !m_frozen && !m_freeCamera && !sceneHoldsPlayer() && stepLockPick(pad);
    const Pad& playerPad = m_frozen || m_freeCamera || sceneHoldsPlayer() || picking ? kStill : pad;

    // The characters' update, then the cameras' (human::Player keeps that order).
    applyAnimOverrides();
    const anim::Vec3 before = m_player->human().position();
    giveObjectTargets();
    m_player->update(playerPad, &m_scenery->collision(), m_combatants);
    for (Target& target : m_targets) {
        target.human->step();
    }
    m_ai->capture();
    stepPickups();
    stepMugMeter(m_player->human(), playerPad);
    stepLockPickDial();
    stepFlash();
    stepObjects();
    stepHats();
    const anim::Vec3 after = m_player->human().position();
    m_stats.travelled += std::hypot(after.x - before.x, after.y - before.y);
    if (m_trace) {
        *m_trace << human::traceLine(++m_traceSteps, *m_player);
    }
    if (const std::uint32_t id = m_player->human().animator().animId(); id != m_lastAnimId) {
        m_print(std::format("frame {}: clip {}\n", frame.index, id));
        m_lastAnimId = id;
    }
    // The scenes, with any player's skip buttons.
    stepScenes(millisecondsOf(frame.gameTicks),
               static_cast<std::uint16_t>(pad.buttons() | stack.pads().port(1).buttons()));

    // The scenery's step around the camera, then its visibility pass from the newest step's camera. The pinned
    // camera, a scene camera while one is current, or the free camera while it is on, takes the follow camera's
    // place.
    const WorldView stepView = stepSceneryView(frame);
    const world::Vec3 eye = stepView.pose.position;
    // The lights' flicker and the player's shadow dimming, stepped with the simulation.
    m_lights->step(stepView, m_scenery->collision(), m_player->human().position(),
                   static_cast<std::uint32_t>(std::lround(frame.seconds * 1000.0)));
    m_scenery->findVisible(stepView);
    // The world objects round the camera, after the scripts and the scenes moved them.
    stepWorldObjects(eye, stepView, static_cast<std::uint32_t>(std::lround(frame.seconds * 1000.0)));
    // The HUD's step (docs/research/hud.md#the-huds-frame), with the player's rage on panel 0.
    hud::HudFrame hudFrame;
    hudFrame.nowMs = millisecondsOf(frame.gameTicks);
    hudFrame.pads.at(0) = &playerPad;
    // The fixed-camera icon: a locked camera (type 1) or switch 0 off ignores the right stick. Coney has no fixed (0),
    // transition (5) or rail (9) camera.
    if (const camera::Cameras* cameras = m_cast.cameras; cameras != nullptr) {
        hudFrame.cameraIgnoresStick.at(0) =
            cameras->current().kind == camera::CameraKind::Locked || !cameras->enabled(camera::Cameras::kSwitchStick);
    }
    hudFrame.levelNumber = m_levelNumber;
    // A scene's letterbox in or moving hides the HUD.
    hudFrame.letterbox = m_stage->barHeight(hudFrame.nowMs) > 0.0F;
    const combat::RageMeter& rage = m_player->human().fighter().combat().rage();
    hudFrame.players.at(0).rage = rage.value();
    hudFrame.players.at(0).rageMax = rage.maximum();
    hudFrame.players.at(0).raging = rage.raging();
    // Player 1's radar: his feet, facing and speed, and the camera's facing in the humans' convention (0 faces +y,
    // positive to the left); RenderWare's (x, y, z) is the game's (x, -z, y).
    hudFrame.radar.known = true;
    hudFrame.radar.position = m_player->human().position();
    hudFrame.radar.heading = m_player->human().heading();
    hudFrame.radar.speed = m_player->human().speed();
    hudFrame.radar.cameraHeading = std::atan2(-stepView.pose.forward.x, -stepView.pose.forward.z);
    // The score, the money and the four item counters, which the original's panel reads from the stats object and the
    // inventory itself (docs/research/hud.md#item-counters).
    if (m_pickups != nullptr) {
        hud::PanelValues& values = hudFrame.players.at(0);
        values.score = m_pickups->score(0);
        values.money = m_pickups->carried(0, item::kMoney);
        // **Coney's stand-in** for the handcuff counter's `Human_GetCuffCount` (not traced): inventory item 5.
        values.items = {m_pickups->carried(0, item::kRevive), m_pickups->carried(0, item::kSprayPaint),
                        m_pickups->carried(0, item::kHandcuffs), m_pickups->carried(0, item::kHandcuffKey)};
    }
    m_hud->step(hudFrame);
    // The health rings, after the HUD's step: hidden with it.
    stepRings(playerPad, stepView, hudFrame.nowMs);
    ++m_stats.frames;
    // `--freeze-world`: the world stops here, its animations held at this step's time.
    if (m_freezeWorld && !m_frozenSince) {
        m_frozenSince = frame.gameTicks;
        m_scenery->freezeAnimation(millisecondsOf(frame.gameTicks));
        m_print(std::format("world: frozen after frame {}\n", frame.index));
    }
    return ModeResult::Stay;
}

void PlayLevelMode::applyAnimOverrides() {
    for (human::Human* human : m_player->humans().humans()) {
        const human::ScriptState& script = human->script();
        // The idle's replacement (HuUseAnim slot 0).
        const std::string wanted(script.animOverride(human::kUseAnimIds.front()));
        if (wanted != human->idleClipName()) {
            const anim::AnimClip* clip = m_dynamicClips.find(wanted);
            if (!wanted.empty()) {
                m_print(clip != nullptr ? std::format("anim: {} replaces a human's idle\n", wanted)
                                        : std::format("anim: {} not loaded; the idle plays\n", wanted));
            }
            human->setIdleClip(wanted, clip);
        }
        // Every other id's: each slot's clip put in when it changed, each one no longer in a slot taken out.
        for (const human::AnimOverride& slot : script.animOverrides) {
            if (slot.clip.empty() || slot.animId == human::kUseAnimIds.front()) {
                continue;
            }
            const auto applied = human->overrideClipNames().find(slot.animId);
            if (applied == human->overrideClipNames().end() || applied->second != slot.clip) {
                const anim::AnimClip* clip = m_dynamicClips.find(slot.clip);
                m_print(clip != nullptr ? std::format("anim: {} replaces a human's anim {}\n", slot.clip, slot.animId)
                                        : std::format("anim: {} not loaded; anim {} plays\n", slot.clip, slot.animId));
                human->setOverrideClip(slot.animId, slot.clip, clip);
            }
        }
        std::vector<std::uint32_t> gone;
        for (const auto& [id, name] : human->overrideClipNames()) {
            if (script.animOverride(id).empty()) {
                gone.push_back(id);
            }
        }
        for (const std::uint32_t id : gone) {
            human->setOverrideClip(id, {}, nullptr);
        }
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
        m_stage->skinPuppets(alpha,
                             [](const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet,
                                float heading, std::vector<anim::Vec3>& positions, std::vector<anim::Vec3>& normals) {
                                 skin(character, pose, feet, heading, 0.0F, positions, normals);
                             });
        skin(playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading, snapshot.lean, m_positions, m_normals);
        m_hatDraws.clear();
        poseHat(m_player->human(), playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading);
        m_mesh->update(m_positions, m_normals);
        const raycast::CollisionMesh& ground = m_scenery->collision();
        m_lights->addShadow(ground, snapshot.feet);
        ringFeet[ringKey(&m_player->human())] = snapshot.feet;
        for (Target& target : m_targets) {
            const human::TargetSnapshot pose =
                human::interpolate(target.human->previous(), target.human->current(), alpha);
            skin(*m_character, pose.pose, pose.feet, pose.heading, 0.0F, target.positions, target.normals);
            target.mesh->update(target.positions, target.normals);
            m_lights->addShadow(ground, pose.feet);
            ringFeet[ringKey(target.human.get())] = pose.feet;
        }
        const std::vector<ai::AiHuman>& fighters = m_ai->humans();
        for (std::size_t i = 0; i < fighters.size() && i < m_fighterMeshes.size(); ++i) {
            // A deleted human is not drawn.
            if (fighters[i].removed) {
                m_fighterMeshes[i].hidden = true;
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
            FighterMesh& mesh = m_fighterMeshes[i];
            skin(*mesh.character, pose.pose, pose.feet, pose.heading, 0.0F, mesh.positions, mesh.normals);
            poseHat(*fighters[i].human, *mesh.character, pose.pose, pose.feet, pose.heading);
            mesh.mesh->update(mesh.positions, mesh.normals);
            m_lights->addShadow(ground, pose.feet);
            ringFeet[ringKey(fighters[i].human.get())] = pose.feet;
        }
    }
    // Over the frame, before its present: the level's screen effects, the HUD's sprites of the newest step, then the
    // scene's letterbox and fade. The scenery draws itself through the blended view, with the character and the debug
    // lines among its objects.
    if (m_levelEffects) {
        m_engine.addFrameOverlay([this](RenderEngine& engine) { m_levelEffects->drawOverlay(engine); });
    }
    // The blur pulse, then the level's screen tint. They cover the HUD, as the original's screen effects follow it,
    // except while a blur pulse runs: then they go first and the HUD stays sharp over them
    // (docs/research/rendering.md#tint).
    const bool effectsFirst = m_levelEffects && m_levelEffects->screenEffectsFirst();
    if (effectsFirst) {
        m_engine.addFrameOverlay([this](RenderEngine& engine) { m_levelEffects->drawScreenEffects(engine); });
    }
    m_engine.addFrameOverlay([this](RenderEngine& engine) { m_hud->draw(engine); });
    if (m_levelEffects && !effectsFirst) {
        m_engine.addFrameOverlay([this](RenderEngine& engine) { m_levelEffects->drawScreenEffects(engine); });
    }
    m_engine.addFrameOverlay([this, nowMs](RenderEngine& engine) { m_stage->drawOverlay(engine, nowMs); });
    // A layer over all of them (the pause menu), added last so it draws last.
    if (m_overlay != nullptr) {
        m_engine.addFrameOverlay([overlay = *m_overlay](RenderEngine& engine) { overlay(engine); });
    }
    m_scenery->draw(m_engine, blended, millisecondsOf(time.gameTicks), [this, &snapshot, &blended, &ringFeet] {
        // The cars take the objects' lights, each atomic for its own sphere (**Coney's choice**: the original selects
        // them once for the clump's sphere).
        const auto drawCars = [this](graphics::CarPass pass) {
            if (m_levelEffects) {
                m_levelEffects->drawCars([this](rw::Atomic* atomic) { m_lights->drawObject(atomic); }, pass);
            }
        };
        // The humans and their blob shadows, the cars' opaque parts, the world objects; then the see-through panes and
        // the cars' glass; then the health rings over the shadows.
        drawCharacter();
        drawCars(graphics::CarPass::Opaque);
        drawWorldObjects(snapshot);
        drawGlass();
        drawCars(graphics::CarPass::Glass);
        drawRings(ringFeet);
        drawDebugLines(snapshot);
        if (m_levelEffects) {
            m_levelEffects->drawInScene(blended.pose);
            m_levelEffects->drawGlints(m_glints.sprites(), blended.pose);
        }
    });
}

void PlayLevelMode::renderWithOverlay(const RenderTime& time,
                                      const std::function<void(graphics::RenderDevice&)>& overlay) {
    m_overlay = &overlay;
    render(time);
    m_overlay = nullptr;
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

void PlayLevelMode::drawDebugLines(const human::PlayerSnapshot& snapshot) const {
    if (m_debugDraw == nullptr || !m_engine.drawsPixels()) {
        return;
    }
    const debug::DebugDrawOptions& options = *m_debugDraw;
    LineList lines;
    const anim::Vec3 feet = snapshot.feet;
    // The collision triangles whose first corner is near the feet, as a wireframe, up to a budget.
    if (options.collision) {
        const raycast::CollisionMesh& mesh = m_scenery->collision();
        const auto vertices = mesh.vertices();
        const float reach = options.collisionRadius * options.collisionRadius;
        std::size_t drawn = 0;
        for (const raycast::CollisionTriangle& triangle : mesh.triangles()) {
            const raycast::Vec3 a = vertices[triangle.vertices[0]];
            const float dx = a.x - feet.x;
            const float dy = a.y - feet.y;
            const float dz = a.z - feet.z;
            if (dx * dx + dy * dy + dz * dz > reach) {
                continue;
            }
            const raycast::Vec3 b = vertices[triangle.vertices[1]];
            const raycast::Vec3 c = vertices[triangle.vertices[2]];
            constexpr graphics::Rgba kWire{80, 220, 255, 255};
            lines.add({a.x, a.y, a.z}, {b.x, b.y, b.z}, kWire);
            lines.add({b.x, b.y, b.z}, {c.x, c.y, c.z}, kWire);
            lines.add({c.x, c.y, c.z}, {a.x, a.y, a.z}, kWire);
            if (++drawn == kMaxWireTriangles) {
                break;
            }
        }
    }
    // The player: a cross at the feet, the heading, and the velocity (a second's travel).
    if (options.player) {
        constexpr graphics::Rgba kPlayer{255, 230, 60, 255};
        lines.cross(feet, kMarkerSize, kPlayer);
        const anim::Vec3 ahead = human::facing(snapshot.heading);
        lines.add(feet, anim::add(feet, anim::scale(ahead, kHeadingLength)), kPlayer);
        lines.add(feet, anim::add(feet, m_player->human().velocity()), graphics::Rgba{255, 120, 40, 255});
    }
    if (options.groundNormal) {
        lines.add(feet, anim::add(feet, m_player->human().groundNormal()), graphics::Rgba{120, 255, 120, 255});
    }
    // The follow camera: where it wants to be and what it looks at.
    if (options.camera) {
        constexpr graphics::Rgba kCamera{255, 80, 220, 255};
        lines.cross(m_player->camera().wanted(), kMarkerSize, kCamera);
        lines.cross(snapshot.cameraTarget, kMarkerSize, kCamera);
        lines.add(snapshot.cameraEye, snapshot.cameraTarget, kCamera);
    }
    // The scene's places, each a cross and its heading.
    if (options.places) {
        constexpr graphics::Rgba kPlace{255, 255, 255, 255};
        for (const debug::Place& place : m_scenery->places()) {
            lines.cross(place.feet, kMarkerSize * 2.0F, kPlace);
            const anim::Vec3 ahead = human::facing(place.headingDegrees * std::numbers::pi_v<float> / 180.0F);
            lines.add(place.feet, anim::add(place.feet, anim::scale(ahead, kHeadingLength)), kPlace);
        }
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
                       human.airborne() ? "airborne" : "grounded", human::traversalName(human.traversal()),
                       human.stamina().value(), human.stamina().maximum(), human.sprinting() ? ", sprint" : "");
}

void PlayLevelMode::teleport(const debug::Place& place) {
    // Dropped onto the ground below the spot, as a start is; left where it is over nothing.
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    raycast::Vec3 point{place.feet.x, place.feet.y, place.feet.z + kTeleportDrop};
    if (!raycast::dropToGround(mesh, kTeleportDrop * 2.0F, point)) {
        point.z = place.feet.z;
    }
    m_player->teleport(&mesh, human::PlayerStart{.position = anim::Vec3{point.x, point.y, point.z},
                                                 .headingDegrees = place.headingDegrees});
}

void PlayLevelMode::startAt(const StartPlace& place) {
    // Dropped onto the ground below the spot, as the debug menus' teleport is; then the camera, when given.
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    raycast::Vec3 point{place.x, place.y, place.z + kTeleportDrop};
    if (!raycast::dropToGround(mesh, kTeleportDrop * 2.0F, point)) {
        point.z = place.z;
    }
    m_player->teleport(&mesh, human::PlayerStart{.position = anim::Vec3{point.x, point.y, point.z},
                                                 .headingDegrees = place.headingDegrees});
    if (place.cameraDistance.has_value() && place.cameraYawDegrees.has_value()) {
        m_player->placeCamera(*place.cameraDistance, *place.cameraYawDegrees * std::numbers::pi_v<float> / 180.0F);
    }
}

void PlayLevelMode::teleportPlayer(const world_objects::Placement& placement) {
    // TeleportToFlag sets the transform without a ground snap: spawned without the mesh.
    const std::array<float, 3>& p = placement.position;
    m_player->teleport(nullptr, human::PlayerStart{.position = anim::Vec3{p[0], p[1], p[2]},
                                                   .headingDegrees = placement.headingDegrees});
}

bool PlayLevelMode::startTagSpray(const std::array<float, 3>& point) {
    return m_player->human().startTagSpray(anim::Vec3{point[0], point[1], point[2]});
}

bool PlayLevelMode::tagSprayLooping() const { return m_player->human().tagSprayLooping(); }

bool PlayLevelMode::tagSprayPlaying() const { return m_player->human().tagSprayPlaying(); }

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

std::expected<void, Error> PlayLevelMode::spawn(const sandbox::Primitive& primitive) {
    std::vector<sandbox::Primitive> next = m_spawned;
    next.push_back(primitive);
    if (auto rebuilt = m_scenery->setExtras(m_engine, next); !rebuilt) {
        return rebuilt;
    }
    m_spawned = std::move(next);
    return {};
}

std::expected<void, Error> PlayLevelMode::clearSpawned() {
    if (auto rebuilt = m_scenery->setExtras(m_engine, {}); !rebuilt) {
        return rebuilt;
    }
    m_spawned.clear();
    return {};
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
        anim::distance(c, m_player->camera().lookAt()), fight, m_scenery->summary(),
        m_scenes != nullptr ? m_stage->summary() + scenesSummary(m_scenes->stats()) : std::string{});
}

} // namespace coney::platform
