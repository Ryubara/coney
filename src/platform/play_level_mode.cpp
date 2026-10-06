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
#include "characters/character_data.h"
#include "characters/character_rig.h"
#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "human/human_animator.h"
#include "human/player_trace.h"
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
    return std::unique_ptr<PlayLevelMode>(new PlayLevelMode(engine, wad, std::move(scenery), std::move(*loaded),
                                                            std::move(print), std::move(model), used, cast));
}

PlayLevelMode::PlayLevelMode(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
                             LoadedCharacter loaded, std::function<void(std::string_view)> print, std::string model,
                             const PlayerSetup& setup, const ScriptedCast* cast)
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
    m_mesh = std::make_unique<CharacterMesh>(m_character->assets().model, m_texture);
    m_lights = std::make_unique<CharacterLights>(kCharacterAmbient, kCharacterDirectional,
                                                 directionToRenderWare(m_scenery->lightDirection()));
    makeTargets(m_texture);
    // The brains plan their moves on the level's routes, when it has path data.
    if (const world::PathMap* paths = m_scenery->pathMap(); paths != nullptr) {
        m_planner = std::make_unique<ai::RoutePlanner>(*paths);
    }
    // The AI humans in the player's step: the level's scripts' humans, or the layout's fighters.
    if (cast != nullptr && cast->brains != nullptr && cast->scripted != nullptr) {
        makeCast(*cast, setup.ai);
    } else {
        m_ai = std::make_unique<ai::AiHumans>(*m_player, *m_character, setup.ai);
        m_ai->brains().setPlanner(m_planner.get());
        for (const sandbox::FighterPoint& point : m_scenery->fighters()) {
            addFighter(point.position, point.headingDegrees);
        }
        if (m_ai->count() > 0) {
            m_print(std::format("fighters: {} from the layout ({} configuration calls read)\n", m_ai->count(),
                                m_ai->config().callsRead));
        }
    }
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
}

PlayLevelMode::~PlayLevelMode() {
    attachScenes(nullptr, 0.0); // the scenes may outlive the stage they were hosted by
    m_lights.reset();
    m_ai.reset(); // out of the player's step before he goes
    m_fighterMeshes.clear();
    m_targets.clear(); // their meshes too hold the texture
    m_mesh.reset();    // before the dictionaries, whose texture it holds
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
    // The follow camera, in RenderWare's axes.
    const world::Vec3 position = toRenderWare(snapshot.cameraEye);
    const anim::Vec3 look = anim::subtract(snapshot.cameraTarget, snapshot.cameraEye);
    const anim::Vec3 forwardGame = anim::length(look) > 1e-6F ? anim::normalise(look) : anim::Vec3{0.0F, 1.0F, 0.0F};
    const anim::Vec3 forward = directionToRenderWare(forwardGame);
    const anim::Vec3 worldUp{0.0F, 1.0F, 0.0F};
    anim::Vec3 right = anim::cross(forward, worldUp);
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
    const graphics::Extent size = m_engine.frameSize();
    const float aspect =
        size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 4.0F / 3.0F;
    return WorldView{.pose = pose,
                     .halfWidth = window.halfHeight * aspect,
                     .halfHeight = window.halfHeight,
                     .nearClip = lens.nearClip,
                     .drawDistance = drawDistance};
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
    m_lights->use();
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    m_mesh->atomic()->render();
    for (const Target& target : m_targets) {
        target.mesh->atomic()->render();
    }
    for (const FighterMesh& fighter : m_fighterMeshes) {
        fighter.mesh->atomic()->render();
    }
    m_stage->drawPuppets();
}

ModeResult PlayLevelMode::update(GameModeStack& stack, const FrameTime& frame) {
    // The draw distance render() blends moves on a step.
    m_drawDistance.commit();

    // The free camera, when on, takes pad 1 and the player stands still, as a frozen one does.
    const Pad& pad = stack.pads().port(0);
    if (m_freeCamera) {
        m_freeCamera->commit();
        m_freeCamera->current().update(pad, static_cast<float>(frame.seconds));
    }
    static const Pad kStill;
    const Pad& playerPad = m_frozen || m_freeCamera || sceneHoldsPlayer() ? kStill : pad;

    // The characters' update, then the cameras' (human::Player keeps that order).
    const anim::Vec3 before = m_player->human().position();
    m_player->update(playerPad, &m_scenery->collision(), m_combatants);
    for (Target& target : m_targets) {
        target.human->step();
    }
    m_ai->capture();
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

    // The scenery's step around the camera (for a level: one streaming decision and the draw distance), then its
    // visibility pass from the newest step's camera: the next step's streaming reads it, so it belongs to the
    // simulation, not to the blended render.
    // With the free camera on, the scenery streams and culls round it instead.
    // A scene camera, while one is current, likewise.
    const std::optional<WorldView> sceneView = m_stage->cameraView(1.0F, m_engine.frameSize());
    const world::Vec3 eye = sceneView      ? sceneView->pose.position
                            : m_freeCamera ? m_freeCamera->current().position()
                                           : toRenderWare(m_player->current().cameraEye);
    m_scenery->step(eye, frame);
    m_drawDistance.current() = m_scenery->drawDistance();
    m_scenery->findVisible(sceneView      ? *sceneView
                           : m_freeCamera ? viewFrom(m_freeCamera->current().pose(), m_drawDistance.current())
                                          : view(m_player->current(), m_drawDistance.current()));
    ++m_stats.frames;
    return ModeResult::Stay;
}

void PlayLevelMode::render(const RenderTime& time) {
    // Everything drawn comes from the player's snapshots and the draw distance, `alpha` of the way from the step before
    // to the newest one.
    human::PlayerSnapshot snapshot = human::interpolate(m_player->previous(), m_player->current(), time.alpha);
    const float drawDistance = lerp(m_drawDistance.previous(), m_drawDistance.current(), time.alpha);
    // The scene camera while one is current; and player 1 as a scene poses him while it holds him.
    const std::optional<WorldView> sceneView = m_stage->cameraView(time.alpha, m_engine.frameSize());
    const WorldView blended = sceneView ? *sceneView
                              : m_freeCamera
                                  ? viewFrom(blendedFreeCamera(*m_freeCamera, time.alpha).pose(), drawDistance)
                                  : view(snapshot, drawDistance);
    if (const std::optional<scenes::RoleFrame> posed =
            sceneHoldsPlayer() ? m_stage->frameOf(m_playerHandle, time.alpha) : std::nullopt) {
        snapshot.pose = posed->pose;
        snapshot.feet = posed->feet;
        snapshot.heading = posed->heading;
        snapshot.lean = 0.0F;
    }
    if (m_engine.drawsPixels()) {
        m_stage->skinPuppets(time.alpha,
                             [](const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet,
                                float heading, std::vector<anim::Vec3>& positions, std::vector<anim::Vec3>& normals) {
                                 skin(character, pose, feet, heading, 0.0F, positions, normals);
                             });
        skin(playerCharacter(), snapshot.pose, snapshot.feet, snapshot.heading, snapshot.lean, m_positions, m_normals);
        m_mesh->update(m_positions, m_normals);
        for (Target& target : m_targets) {
            const human::TargetSnapshot pose =
                human::interpolate(target.human->previous(), target.human->current(), time.alpha);
            skin(*m_character, pose.pose, pose.feet, pose.heading, 0.0F, target.positions, target.normals);
            target.mesh->update(target.positions, target.normals);
        }
        const std::vector<ai::AiHuman>& fighters = m_ai->humans();
        for (std::size_t i = 0; i < fighters.size() && i < m_fighterMeshes.size(); ++i) {
            human::TargetSnapshot pose = human::interpolate(fighters[i].previous, fighters[i].current, time.alpha);
            // A cast human a scene holds is drawn as the scene poses it.
            if (const double handle = m_scenes != nullptr ? castHandleOf(fighters[i]) : 0.0;
                handle != 0.0 && m_stage->holds(handle)) {
                if (const std::optional<scenes::RoleFrame> posed = m_stage->frameOf(handle, time.alpha)) {
                    pose.pose = posed->pose;
                    pose.feet = posed->feet;
                    pose.heading = posed->heading;
                }
            }
            FighterMesh& mesh = m_fighterMeshes[i];
            skin(*mesh.character, pose.pose, pose.feet, pose.heading, 0.0F, mesh.positions, mesh.normals);
            mesh.mesh->update(mesh.positions, mesh.normals);
        }
    }
    // The scenery draws itself through the blended view, with the character and the debug lines among its objects;
    // the scene's letterbox and fade go over it.
    m_engine.setFrameOverlay(
        [this, nowMs = millisecondsOf(time.gameTicks)](RenderEngine& engine) { m_stage->drawOverlay(engine, nowMs); });
    m_scenery->draw(m_engine, blended, millisecondsOf(time.gameTicks), [this, &snapshot] {
        drawCharacter();
        drawDebugLines(snapshot);
    });
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
