// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/gameplay_mode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/route_planner.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_hub.h"
#include "ai/scripted_humans.h"
#include "ai/scripted_story.h"
#include "ai/spawners.h"
#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "camera/cameras.h"
#include "characters/character_types.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "effects/ground_fog.h"
#include "effects/level_effects.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/pause_mode.h"
#include "gamemodes/player_frame.h"
#include "gamemodes/system_music.h"
#include "hud/hud.h"
#include "human/human.h"
#include "raycast/collision_mesh.h"
#include "scripting/anim_callbacks.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/object_bindings.h"
#include "scripting/sound_bindings.h"
#include "warriors/crime_reports.h"
#include "world_objects/spawn_records.h"
#include "world_objects/volume_boxes.h"

namespace coney {

namespace {

// A car's message: a hit's damaged part, its fire or its explosion (docs/research/cars.md).
constexpr int kCarMessage = 0x19;
// The materials a camera sees through: 30 `LOW_FENCE`, 2 `GLASS`, 122 `RAILING` and 107 `CHAINLINK_NOCLIMB`
// (docs/research/ai.md#spawner-unseen).
constexpr std::array<std::uint8_t, 4> kSeeThroughMaterials{30, 2, 122, 107};
// The largest float below 1, so a draw stays in [0, 1).
constexpr float kBelowOne = 0.99999994F;

// The level as the gangs' spawners see it: player 1 and the bound humans from the scripts' hold, the route graph and
// player 1's camera for the spots out of sight, the gangs' turf boxes, and a new human made by the scripts' own
// `HuCreate`, so it is kept, numbered and made in the world as a script's would be.
class ScriptSpawnerWorld final : public ai::SpawnerWorld {
  public:
    ScriptSpawnerWorld(ai::ScriptedBrains& scripted, script::ScriptSystem& scripts, const camera::Cameras* cameras,
                       const world_objects::VolumeBoxes* boxes, const raycast::CollisionMesh* collision,
                       GameRandom& random)
        : m_scripted(&scripted), m_scripts(&scripts), m_cameras(cameras), m_boxes(boxes), m_collision(collision),
          m_random(&random) {}

    [[nodiscard]] std::optional<anim::Vec3> outOfSight(float value, int gang) override {
        const std::optional<anim::Vec3> player = playerPosition();
        const ai::RoutePlanner* planner = m_scripted->owner().planner();
        if (!player || planner == nullptr) {
            return std::nullopt;
        }
        // Coney choice: with no camera the player's own position is the eye, looking along +y.
        ai::PlacementCamera camera;
        camera.eye = *player;
        if (m_cameras != nullptr) {
            const camera::CameraView& view = m_cameras->view();
            camera =
                ai::PlacementCamera{.eye = view.position,
                                    .forward = anim::normalise(anim::subtract(view.lookAt, view.position)),
                                    .halfFovRadians = view.fieldOfView * 0.5F * std::numbers::pi_v<float> / 180.0F};
        }
        // `Random_Float`: the game's next random number over 2^32.
        const ai::PlacementRandom random = [this] {
            constexpr double kRange = 4294967296.0;
            return std::min(static_cast<float>(static_cast<double>(m_random->next()) / kRange), kBelowOne);
        };
        const std::optional<anim::Vec3> spot = ai::outOfSightNode(*planner, *player, camera, value, random);
        if (!spot || !inTurf(gang, *spot)) {
            return std::nullopt;
        }
        return spot;
    }

    [[nodiscard]] std::optional<anim::Vec3> playerPosition() const override {
        const ai::Brain* player = m_scripted->player();
        return player != nullptr ? std::optional(player->human().position()) : std::nullopt;
    }

    // `Camera_AnyPlayerCanSeePoint` with player 1's camera (Coney has one): the sphere is seen when it lies within the
    // view distance, reaches into the view and no wall of the collision mesh stands between the eye and its centre
    // (low fences, glass, railings and unclimbable chain-link do not hide it). Coney stand-in for the six-plane
    // frustum test: a cone of half the field of view, widened by the sphere. No camera sees nothing.
    // @orig 0x001202e8 Camera_AnyPlayerCanSeePoint (unknown)
    // @orig 0x00122548 Camera_CanSeePoint (unknown)
    [[nodiscard]] bool seen(anim::Vec3 centre, float radius) const override {
        if (m_cameras == nullptr) {
            return false;
        }
        const camera::CameraView& view = m_cameras->view();
        const anim::Vec3 toCentre = anim::subtract(centre, view.position);
        const float distance = anim::length(toCentre);
        if (distance - radius > view.farClip) {
            return false;
        }
        if (distance > radius) {
            const anim::Vec3 forward = anim::normalise(anim::subtract(view.lookAt, view.position));
            const float angle = std::acos(std::clamp(anim::dot(toCentre, forward) / distance, -1.0F, 1.0F));
            const float halfFov = view.fieldOfView * 0.5F * std::numbers::pi_v<float> / 180.0F;
            if (angle > halfFov + std::asin(radius / distance)) {
                return false;
            }
        }
        // The occlusion ray to the centre, as long as the distance less the radius.
        constexpr float kRayShort = 1e-5F;
        const float length = distance - radius - kRayShort;
        if (m_collision == nullptr || length <= 0.0F) {
            return true;
        }
        const anim::Vec3 direction = anim::scale(toCentre, 1.0F / distance);
        const raycast::Ray ray{.origin = {view.position.x, view.position.y, view.position.z},
                               .direction = {direction.x, direction.y, direction.z},
                               .length = length};
        return !m_collision->rayCast(ray, kSeeThroughMaterials, 0).has_value();
    }

    [[nodiscard]] bool alive(double handle) const override {
        const auto found = m_scripted->bound().find(handle);
        return found != m_scripted->bound().end() && found->second->human().alive();
    }

    double spawn(const ai::SpawnRequest& request) override {
        auto position = std::make_shared<script::Table>();
        for (std::size_t i = 0; i < request.position.size(); ++i) {
            if (!position->set(script::Value(static_cast<double>(i + 1)), script::Value(request.position.at(i)))) {
                return 0.0;
            }
        }
        const std::array<script::Value, 7> args{script::Value(request.name),
                                                script::Value(static_cast<double>(request.type)),
                                                script::Value(std::move(position)),
                                                script::Value(static_cast<double>(request.heading)),
                                                script::Value(request.model),
                                                script::Value(0.0),
                                                script::Value(static_cast<double>(request.gang))};
        script::LuaVm& vm = m_scripts->vm();
        const auto made = vm.call(vm.global("HuCreate"), args);
        if (!made || made->empty()) {
            return 0.0;
        }
        return made->front().number().value_or(0.0);
    }

    void spawned(std::string_view callback, double handle, int gang, std::string_view spawner) override {
        // A name that is not a function calls nothing, without a message (0x0016d860 checks before it calls).
        if (!m_scripts->hasFunction(callback)) {
            return;
        }
        const std::array<script::Value, 3> args{script::Value(handle), script::Value(static_cast<double>(gang)),
                                                script::Value(std::string(spawner))};
        m_scripts->call(callback, args);
    }

  private:
    // Whether `point` lies in one of the gang's turf boxes; a gang with no turf (or no boxes kept) takes any point.
    [[nodiscard]] bool inTurf(int gang, anim::Vec3 point) const {
        return ai::pointInTurf(m_scripted->owner().gangs().find(gang), m_boxes, point);
    }

    ai::ScriptedBrains* m_scripted;
    script::ScriptSystem* m_scripts;
    const camera::Cameras* m_cameras;
    const world_objects::VolumeBoxes* m_boxes;
    const raycast::CollisionMesh* m_collision;
    GameRandom* m_random;
};

} // namespace

GameplayMode::GameplayMode(graphics::RenderDevice& device, script::ScriptSystem& scripts,
                           script::BindingContext& context, GameState& state, CreatedHumans& humans,
                           world_objects::WorldFlags& flags, const script::RecordedCalls& recorded, LevelLoader loader,
                           std::function<void(std::string_view)> log)
    : m_device(device), m_scripts(scripts), m_context(context), m_state(state), m_humans(humans), m_flags(flags),
      m_recorded(recorded), m_loader(std::move(loader)), m_log(std::move(log)),
      m_objectServices(scripts, flags, nullptr) {
    m_objectServices.setPlayers(&state, &humans);
    // Damage a human does reaches the volume boxes he stands in as their message 6, (human, box, object).
    m_objectServices.setDamageReceiver([this](double human, double object) { sendDamageMessage(human, object); });
    // A human's car hit reaches the car's own handler and the cars' general one as message 0x19.
    m_objectServices.setCarHitReceiver([this](double car, double human, int part, bool broke) {
        if (m_context.messages != nullptr) {
            static_cast<void>(m_context.messages->deliverFromCar(m_scripts, car, kCarMessage, human,
                                                                 static_cast<double>(part), broke));
        }
    });
}

GameplayMode::~GameplayMode() {
    endLevel();
    if (m_context.objects == &m_objects) {
        m_context.objects = nullptr;
    }
    if (m_context.crimes == &m_objectServices.crimeServices()) {
        m_context.crimes = nullptr;
    }
}

void GameplayMode::endLevel() {
    // The level first: its humans are what the brains refer to, its player what the cameras follow. Then the cameras,
    // whose locator reads the scripts' hold, then the hold, then the brains it holds.
    m_level.reset();
    if (m_context.cameras == m_cameras.get()) {
        m_context.cameras = nullptr;
    }
    m_cameras.reset();
    // The level's objects go with it, and with them the level's collision mesh and path data they pointed at.
    m_objects.clear();
    m_objects.world = world_objects::ObjectWorld{.services = &m_objectServices, .random = &m_state.random};
    if (m_context.effects == m_effects.get()) {
        m_context.effects = nullptr;
    }
    m_objectServices.setParticles(nullptr, {});
    m_objectServices.setCars(nullptr);
    if (m_context.cars == m_cars.get()) {
        m_context.cars = nullptr;
    }
    m_cars.reset();
    m_effects.reset();
    // The trigger spheres and the flag network go with the level's objects.
    if (m_context.spheres == &m_spheres) {
        m_context.spheres = nullptr;
    }
    if (m_context.tagSpots == &m_tagSpots) {
        m_context.tagSpots = nullptr;
    }
    m_tagSession.reset();
    m_tagSpots.clear();
    if (m_context.radios == &m_radios) {
        m_context.radios = nullptr;
    }
    // A radio's sound stops with the level.
    if (m_context.sound != nullptr) {
        for (const world_objects::Radio& radio : m_radios.all()) {
            m_context.sound->stopSound(radio.sound);
        }
    }
    m_radios.clear();
    if (m_context.flagNet == &m_flagNet) {
        m_context.flagNet = nullptr;
    }
    m_spheres.clear();
    m_flagNet.clear();
    if (m_context.ai == m_scripted.get()) {
        m_context.ai = nullptr;
    }
    m_scripted.reset();
    m_brains.reset();
    if (m_scenes && m_context.scenes == m_scenes.get()) {
        m_context.scenes = m_scenesBefore; // the front end's, say, again
    }
    m_scenes.reset();
    if (m_context.lighting == m_lighting.get()) {
        m_context.lighting = nullptr;
    }
    m_lighting.reset();
}

void GameplayMode::enter() {
    // The level's brains and gangs (InitLevel's AI reset), which its script's bindings drive. The calls on the humans
    // the script creates wait until the level has loaded its characters and made them.
    endLevel();
    // Mode 1's audio set-up (docs/research/level-loading.md#mode-1, step 1).
    if (m_context.sound != nullptr) {
        m_context.sound->gameplayEntered();
    }
    m_brains = std::make_unique<ai::Brains>();
    m_scripted = std::make_unique<ai::ScriptedBrains>(*m_brains, m_flags,
                                                      [this](double handle) { return m_humans.placement(handle); });
    m_scripted->setScripts(&m_scripts);
    m_scripted->setMessages(m_context.messages);
    if (m_context.animCallbacks != nullptr) {
        m_context.animCallbacks->clear();
    }
    m_scripted->setAnimCallbacks(m_context.animCallbacks);
    m_scripted->hold();
    // The level's scenes, which the script preloads and plays; their end functions and preload callbacks call it.
    if (m_sceneMaker) {
        m_scenes = m_sceneMaker();
    }
    if (m_scenes) {
        m_scenes->setScriptCall([this](std::string_view function, std::span<const double> args) {
            std::vector<script::Value> values(args.begin(), args.end());
            m_scripts.call(function, values);
        });
        m_scenesBefore = m_context.scenes;
        m_context.scenes = m_scenes.get();
    }
    // The characters' rules the configuration set (the rage handlers, the formations' default slots).
    if (m_context.state != nullptr) {
        m_scripted->humanHost().applyRules(m_context.state->characters);
    }
    // The last level's objects are gone, and their handlers and boxes with them.
    if (m_context.messages != nullptr) {
        m_context.messages->clear();
    }
    if (m_context.boxes != nullptr) {
        m_context.boxes->clear();
        // A human the scripts made still resolves after `HuDelete` (Coney keeps every one for the level).
        m_context.boxes->setResolves([this](double handle) { return m_humans.find(handle) != nullptr; });
    }
    m_context.ai = m_scripted.get();
    // What the hub's bindings read beyond the brains: the configuration's categories and flee percentages, the
    // workout's tuning, the store boxes and their flags, and the crimes.
    wireHub();
    // The crimes the scripts report reach the level's gangs and police; the story's per-level switches start clear.
    m_context.crimes = &m_objectServices.crimeServices();
    if (m_context.state != nullptr) {
        m_context.state->story.resetForLevel();
    }
    // The level's trigger spheres, round the objects the scripts name, and its flag network for the pedestrians.
    m_spheres.setLocate([this](double handle) { return objectPosition(handle); });
    m_spheres.setClearLine([this](const std::array<float, 3>& from, const std::array<float, 3>& to) {
        const raycast::CollisionMesh* mesh = m_objects.world.collision;
        if (mesh == nullptr) {
            return true;
        }
        const raycast::Vec3 d{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        const float length = std::sqrt((d.x * d.x) + (d.y * d.y) + (d.z * d.z));
        if (length < 1e-4F) {
            return true;
        }
        const raycast::Ray ray{.origin = {from[0], from[1], from[2]},
                               .direction = {d.x / length, d.y / length, d.z / length},
                               .length = length};
        return !mesh->rayCast(ray, {}, 0).has_value();
    });
    m_context.spheres = &m_spheres;
    m_radios.clear();
    m_context.radios = &m_radios;
    m_tagSpots.clear();
    m_tagSession.reset();
    m_tagTicks = 0.0;
    m_context.tagSpots = &m_tagSpots;
    m_scripted->storyHost().setTagHandler(
        [this](double human, double tag, double flag) { startTag(human, tag, flag); });
    // HuIsTagging: player 1 in his stick game, or a spot's tagger while the spot paints in.
    m_scripted->humanHost().setTaggingQuery([this](double human) {
        if (m_tagSession && !m_tagSession->ended() && m_tagSession->human() == human) {
            return true;
        }
        return std::ranges::any_of(m_tagSpots.all(), [human](const world_objects::TagSpot& spot) {
            return spot.tagger == human && spot.fadeMode == world_objects::TagSpot::kFadingIn;
        });
    });
    m_uncuff.reset();
    m_uncuffHooked = nullptr;
    m_scripted->humanHost().setArrestHook([this](ai::Brain& brain, bool arrested) { onArrest(brain, arrested); });
    m_context.flagNet = &m_flagNet;
    m_scripted->setFlagNet(&m_flagNet);
    m_scripted->storyHost().setBoxes(m_context.boxes);
    // Player 1's cameras, which the script sets up before the level makes him; CamSetSecondary finds its human live.
    m_cameras = std::make_unique<camera::Cameras>();
    m_cameras->setLocator([scripted = m_scripted.get()](double handle) -> std::optional<anim::Vec3> {
        const std::optional<world_objects::Placement> placement = scripted->humanPlacement(handle);
        if (!placement) {
            return std::nullopt;
        }
        return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
    });
    // CamCanSee's objects: a spawned object, else a pane or door.
    m_cameras->setObjectLocator([this](double handle) -> std::optional<anim::Vec3> {
        if (m_context.spawnRecords != nullptr) {
            if (const world_objects::SpawnRecord* record = m_context.spawnRecords->find(handle)) {
                return anim::Vec3{record->position[0], record->position[1], record->position[2]};
            }
        }
        return m_objects.positionOf(handle);
    });
    // The win camera starts on its target where he stands and faces; player 1 where a teleport the level has not
    // applied yet puts him (the scripts move the winner and start the camera in one frame).
    m_cameras->setPlacer(
        [this, scripted = m_scripted.get()](double handle) -> std::optional<std::pair<anim::Vec3, float>> {
            std::optional<world_objects::Placement> placement = scripted->humanPlacement(handle);
            if (const HumanCreation* player = m_humans.player(1); player != nullptr && player->handle == handle &&
                                                                  player->teleported &&
                                                                  player->teleports != m_playerTeleports) {
                placement = player->teleported;
            }
            if (!placement) {
                return std::nullopt;
            }
            const std::array<float, 3>& p = placement->position;
            return std::pair{anim::Vec3{p[0], p[1], p[2]}, placement->headingDegrees};
        });
    // A locked camera's kept-in-view push moves a human the scripts made, keeping his heading.
    m_cameras->setMover([scripted = m_scripted.get()](double handle, anim::Vec3 feet) {
        if (ai::Brain* brain = scripted->brain(handle); brain != nullptr) {
            brain->human().place(feet, brain->human().heading());
        }
    });
    m_context.cameras = m_cameras.get();
    // The level script spawns the level's panes and doors into gameplay's objects, typed by what the boot scripts'
    // `CfgSetGlassProperties` calls recorded.
    script::applyRecordedGlassTypes(m_recorded, m_objects.glass);
    m_context.objects = &m_objects;
    // A fresh light manager and fog for the level, which its scripts' SetLight and SetFogColor fill.
    m_lighting = std::make_unique<graphics::LevelLighting>();
    m_context.lighting = m_lighting.get();
    // The level's particles and motion blur; an attached particle system follows a human the scripts made.
    m_effects = std::make_unique<effects::LevelEffects>();
    // The litter's rays meet the level's collision, once the level has given the objects theirs.
    m_effects->litterRay = [this](anim::Vec3 from, anim::Vec3 to) -> std::optional<effects::LitterHit> {
        const raycast::CollisionMesh* mesh = m_objects.world.collision;
        const anim::Vec3 d = anim::subtract(to, from);
        const float length = std::sqrt(anim::dot(d, d));
        if (mesh == nullptr || length < 1e-4F) {
            return std::nullopt;
        }
        const raycast::Ray ray{.origin = {from.x, from.y, from.z},
                               .direction = {d.x / length, d.y / length, d.z / length},
                               .length = length};
        const std::optional<raycast::RayHit> hit = mesh->rayCast(ray, {}, 0);
        if (!hit) {
            return std::nullopt;
        }
        return effects::LitterHit{.point = anim::add(from, anim::scale(d, hit->t / length)),
                                  .normal = anim::Vec3{hit->normal.x, hit->normal.y, hit->normal.z}};
    };
    m_effects->particles.setLocator([scripted = m_scripted.get()](double handle) -> std::optional<anim::Vec3> {
        const std::optional<world_objects::Placement> placement = scripted->humanPlacement(handle);
        if (!placement) {
            return std::nullopt;
        }
        return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
    });
    m_context.effects = m_effects.get();
    // The level's parked cars; the police car's lights are particles.
    m_cars = std::make_unique<world_objects::Cars>();
    m_cars->setParticles(&m_effects->particles);
    m_context.cars = m_cars.get();
    m_objectServices.setCars(m_cars.get());
    // The panes' shards and the objects' dust go to the level's particles, culled round player 1.
    m_objectServices.setParticles(&m_effects->particles, [this]() -> std::optional<anim::Vec3> {
        const HumanCreation* player = m_humans.player(1);
        const std::optional<world_objects::Placement> placement =
            player != nullptr && m_scripted ? m_scripted->humanPlacement(player->handle) : std::nullopt;
        if (!placement) {
            return std::nullopt;
        }
        return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
    });

    // With a loading screen the level loads once it has faded in (updateLoadingScreen()); begun by the first update,
    // which knows the time.
    m_screenStartMs.reset();
    if (m_loadingScreen != nullptr) {
        m_phase = Phase::FadeIn;
        return;
    }
    // Without one, the load screen's sounds (InitLevel steps 3 and 11) go straight to the sound around the load.
    m_phase = Phase::Playing;
    if (m_context.sound != nullptr) {
        const LevelRecord* record = m_state.levels.at(m_state.currentLevel);
        m_context.sound->levelLoadStarted(record != nullptr ? static_cast<int>(record->number) : 0);
    }
    loadLevel();
    if (m_context.sound != nullptr) {
        m_context.sound->levelLoaded();
    }
    startPlay();
}

void GameplayMode::loadLevel() {
    startPlayerLevel(m_state);
    // InitLevel's HUD set-up, before the script: the level starts with the HUD hidden until something shows it.
    if (m_context.hud != nullptr) {
        m_context.hud->levelSetUp();
    }
    // InitLevel's script step: the level script creates player 1 at the checkpoint's start, before anything streams.
    const LevelStart& start =
        m_start.emplace(runLevelScript(m_scripts, m_state, m_humans, m_flags, m_levelName, m_context.spawnRecords));
    const HumanCreation* player = start.player ? &*start.player : nullptr;
    m_playerTeleports = player != nullptr ? player->teleports : 0;
    if (player != nullptr) {
        const std::array<float, 3> p =
            player->teleported ? player->teleported->position : player->position.value_or(std::array<float, 3>{});
        const float heading = player->teleported ? player->teleported->headingDegrees : player->headingDegrees;
        m_log(std::format("gameplay: {} checkpoint {}: player 1 {} (type {}, model {}) at ({:.2f}, {:.2f}, {:.2f}) "
                          "heading {:.0f}, {} the level script; {} humans, {} flags\n",
                          start.level, start.checkpoint, player->name, player->type,
                          player->model.empty() ? "unknown" : player->model, p[0], p[1], p[2], heading,
                          player->teleported ? "teleported to a flag by" : "created by", m_humans.all().size(),
                          m_flags.all().size()));
    } else {
        m_log(std::format("gameplay: {} checkpoint {}: the level script made no player 1 with a position\n",
                          start.level, start.checkpoint));
    }
    // The parked cars the script made, one line each.
    for (const world_objects::Car& car : m_cars->all()) {
        m_log(std::format("gameplay: car {} at ({:.2f}, {:.2f}, {:.2f})\n",
                          car.type ? world_objects::kCarTypeNames.at(*car.type) : std::string_view("of no type"),
                          car.position.x, car.position.y, car.position.z));
    }

    // The loose objects a player may pick up: the spawn records the scripts and the placed objects filled.
    m_pickups.reset();
    if (m_context.spawnRecords != nullptr && m_context.objectTypes != nullptr) {
        m_pickups.emplace(m_scripts, m_state, *m_context.spawnRecords, *m_context.objectTypes, m_context.messages);
        m_pickups->setLocator([this](double object) { return promptObjectPosition(object); });
    }
    m_shownPrompt.clear();

    // The level itself, with the player at that start; entering it preloads the world around him.
    std::expected<std::unique_ptr<GameMode>, Error> level = fail(ErrorCode::NotFound, "no level loader");
    if (m_loader) {
        level = m_loader(start, ScriptedCast{.humans = &m_humans,
                                             .recorded = &m_recorded,
                                             .brains = m_brains.get(),
                                             .scripted = m_scripted.get(),
                                             .cameras = m_cameras.get(),
                                             .scenes = m_scenes.get(),
                                             .objects = &m_objects,
                                             .lighting = m_lighting.get(),
                                             .effects = m_effects.get(),
                                             .cars = m_cars.get(),
                                             .pickups = m_pickups ? &*m_pickups : nullptr,
                                             .records = m_context.spawnRecords,
                                             .types = m_context.objectTypes,
                                             .forceReticules = &m_state.forceReticules,
                                             .sound = m_context.sound});
    }
    if (!level) {
        m_log(std::format("gameplay: {}: {}\n", start.level, level.error().message));
        return;
    }
    m_level = std::move(*level);
    m_level->enter();
}

void GameplayMode::startPlay() {
    // A level that failed to load asks for no movie.
    if (!m_level) {
        return;
    }
    // InitLevel step 12, after the preload: the intro movie (`L99_IN` for level99 at checkpoint 1).
    if (const LevelRecord* record = m_state.levels.at(m_state.currentLevel);
        record != nullptr && m_moviePlayer != nullptr) {
        if (const std::optional<std::string> movie = levelIntroMovie(*record, m_state.checkPoint)) {
            m_moviePlayer->playMovie(*movie);
        }
    }
}

bool GameplayMode::updateLoadingScreen(const FrameTime& frame) {
    LoadingScreen& screen = *m_loadingScreen;
    const std::uint64_t msPerTick = GameTimer::kTicksPerSecond / 1000;
    const std::uint64_t nowMs = frame.gameTicks / msPerTick;
    if (!m_screenStartMs) {
        // LoadScreen_Begin (InitLevel step 2), at the moment the mode was entered: just before this first step.
        m_screenStartMs = (frame.gameTicks - frame.stepTicks) / msPerTick;
        const LevelRecord* record = m_state.levels.at(m_state.currentLevel);
        const int number = record != nullptr ? static_cast<int>(record->number) : 0;
        const int gameType = m_state.rumble.values.at(RumbleSetup::kGameType);
        screen.begin(m_levelName, number, gameType, *m_screenStartMs);
    }
    if (m_phase == Phase::FadeIn && nowMs >= *m_screenStartMs + LoadScreenTimeline::kFadeMilliseconds) {
        // Faded in: the load-screen sounds (step 3), then the whole load in this one step (steps 4-10).
        screen.startSounds();
        loadLevel();
        m_phase = Phase::Loading;
    }
    if (m_phase == Phase::Loading && nowMs >= *m_screenStartMs + kLoadScreenHoldMilliseconds) {
        // Step 11: the sounds stop, then LoadScreen_End's finish fades the screen out.
        screen.stopSounds();
        screen.finish(nowMs);
        m_phase = Phase::FadeOut;
    }
    if (m_phase == Phase::FadeOut && screen.finished(nowMs)) {
        // The fade out is over: the next frame cuts to the movie and then play.
        screen.end();
        m_phase = Phase::Playing;
        startPlay();
        return true;
    }
    return false;
}

ModeResult GameplayMode::update(GameModeStack& stack, const FrameTime& frame) {
    // Behind the loading screen nothing plays and the pads are not read.
    if (m_phase != Phase::Playing && m_loadingScreen != nullptr && !updateLoadingScreen(frame)) {
        return ModeResult::Stay;
    }
    // The intro movie the load's end just asked for plays before the level's first frame, as InitLevel blocks in
    // Movie_Play: had the world stepped now, the scripts' first scene would load and prepare its soundtrack, and the
    // movie's stop of every sound would throw it away (docs/research/sound.md#scene-sound).
    if (stack.top() != this) {
        return ModeResult::Stay;
    }
    const ModeResult result = updateWorld(stack, frame);
    // START pauses the game (PauseMenu_Toggle), last in the frame of play.
    if (m_pause != nullptr) {
        m_pause->playFrame(stack, stack.pads());
    }
    return result;
}

ModeResult GameplayMode::updateWorld(GameModeStack& stack, const FrameTime& frame) {
    // The level's step (the characters, the cameras, the streaming), then the scripts' frame, as a frame of play
    // orders them.
    ModeResult result = ModeResult::Stay;
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    updateWarCommandMenu(stack.pads(), nowMs);
    if (m_level) {
        result = m_level->update(stack, frame);
    }
    updateUncuff();
    m_scripts.setTime(nowMs);
    callTutorialCallback();
    callPadHandler();
    // The path camera's functions reached in the step.
    if (m_cameras) {
        for (const std::string& function : m_cameras->takeFired()) {
            m_scripts.call(function, std::vector<script::Value>{});
        }
    }
    if (m_scripted) {
        m_scripted->runAnimCallbacks();
        m_scripted->humanHost().runRageHandlers();
        m_scripted->storyHost().update();
        ScriptSpawnerWorld spawnerWorld(*m_scripted, m_scripts, m_cameras.get(), m_context.boxes,
                                        m_objects.world.collision, m_state.random);
        m_scripted->humanHost().spawners().update(nowMs, spawnerWorld);
        m_scripted->hubHost().update(nowMs);
        stepSystemMusic(m_state.story, m_context.sound, m_state.random, m_scripted->storyHost().musicMood());
    }
    updateBoxes(nowMs);
    if (m_scripted && m_context.messages != nullptr) {
        const std::vector<world_objects::BoxSubject> subjects = m_scripted->boxSubjects();
        m_spheres.update(subjects, nowMs, [this](double object, int message, double human) {
            m_context.messages->deliver(m_scripts, object, message, human, 0.0, 0.0);
        });
    }
    updateRadios();
    updateTagging(stack.pads(), frame.seconds);
    updateActionPrompt();
    runPlayerFrame(m_state, m_scripts, stack.pads(), nowMs, &m_objectServices.crimeServices());
    m_scripts.update(nowMs, frame.seconds);
    if (m_effects) {
        // The camera's view this frame, for the effects that follow it (the steam vents' near test, the fog).
        std::optional<effects::EffectsViewer> viewer;
        if (m_cameras && m_cameras->current().kind != camera::CameraKind::None) {
            const camera::CameraView& view = m_cameras->view();
            viewer = effects::EffectsViewer{.position = view.position, .target = view.lookAt};
        }
        m_effects->step(static_cast<float>(frame.seconds), viewer);
    }

    // A script that teleported player 1 during the frame (the hub's door walk) moves him in the level.
    if (const HumanCreation* player = m_humans.player(1);
        player != nullptr && player->teleported && player->teleports != m_playerTeleports) {
        m_playerTeleports = player->teleports;
        const world_objects::Placement& to = *player->teleported;
        m_log(std::format("gameplay: player 1 teleported to ({:.2f}, {:.2f}, {:.2f}) heading {:.0f}\n", to.position[0],
                          to.position[1], to.position[2], to.headingDegrees));
        if (auto* scripted = dynamic_cast<ScriptedPlayer*>(m_level.get())) {
            scripted->teleportPlayer(to);
        }
    }
    // The screens over play (the Rumble intro) after the scripts' frame.
    for (PlayOverlay* overlay : m_overlays) {
        overlay->playFrame(frame, stack.pads());
    }
    return result;
}

void GameplayMode::updateRadios() {
    if (m_radios.all().empty()) {
        return;
    }
    // The radios' sounds go to the game's sound (none: nothing plays, and every track ends at once).
    class HostSound final : public world_objects::RadioSound {
      public:
        explicit HostSound(script::SoundHost* host) : m_host(host) {}
        double play(std::uint32_t hash, const std::array<float, 3>& position) override {
            return m_host != nullptr && hash != 0 ? m_host->play3D(hash, position) : 0.0;
        }
        [[nodiscard]] bool playing(double handle) const override {
            return m_host != nullptr && m_host->soundPlaying(handle);
        }
        void stop(double handle) override {
            if (m_host != nullptr) {
                m_host->stopSound(handle);
            }
        }
        void follow(double handle, const std::array<float, 3>& position, float volume) override {
            if (m_host != nullptr) {
                m_host->moveSound(handle, position, volume);
            }
        }

      private:
        script::SoundHost* m_host;
    };
    HostSound sound(m_context.sound);
    world_objects::RadioWorld world;
    if (const HumanCreation* player = m_humans.player(1); player != nullptr && m_scripted) {
        world.playerHandle = player->handle;
        if (const std::optional<world_objects::Placement> placement = m_scripted->humanPlacement(player->handle)) {
            world.player = placement->position;
        }
    }
    world.scene = m_scenes && m_scenes->playing();
    world.levelComplete = [this](int level) {
        return m_state.player.unlocks.isLevelComplete(m_state.saved, static_cast<std::uint8_t>(level));
    };
    m_radios.update([this](double object) { return objectPosition(object); }, world, sound, m_state.random,
                    [this](const std::string& function) { m_scripts.call(function, std::vector<script::Value>{}); });
}

void GameplayMode::updateBoxes(std::uint64_t nowMs) {
    if (m_context.boxes == nullptr || m_context.messages == nullptr || !m_scripted) {
        return;
    }
    const std::vector<world_objects::BoxSubject> subjects = m_scripted->boxSubjects();
    m_context.boxes->update(subjects, nowMs, [this](double box, int message, double human) {
        m_context.messages->deliver(m_scripts, box, message, human, 0.0, 0.0);
    });
}

void GameplayMode::sendDamageMessage(double human, double object) {
    if (m_context.boxes == nullptr || m_context.messages == nullptr || !m_scripted) {
        return;
    }
    for (const world_objects::BoxSubject& subject : m_scripted->boxSubjects()) {
        if (subject.handle != human) {
            continue;
        }
        m_context.boxes->sendDamage(subject.position, [this, human, object](double box) {
            m_context.messages->deliver(m_scripts, box, world_objects::VolumeBoxes::kDamaged, human, object, box);
        });
        return;
    }
}

void GameplayMode::callTutorialCallback() {
    if (m_context.hud == nullptr || m_context.hud->tutorialCallback().empty() || !m_scripted ||
        m_scripted->player() == nullptr) {
        return;
    }
    // Copied: the callback may set another (or none) while it runs.
    const std::string callback = m_context.hud->tutorialCallback();
    const std::vector<int> strikes = m_scripted->player()->human().fighter().strikes();
    for (const int animId : strikes) {
        const std::array<script::Value, 1> args{script::Value(static_cast<double>(animId))};
        m_scripts.call(callback, args);
    }
}

void GameplayMode::callPadHandler() {
    if (m_context.state == nullptr || m_context.state->player.pads.targetHandler().empty() || !m_scripted) {
        return;
    }
    // Copied: the handler may set another (or none) while it runs.
    const std::string handler = m_context.state->player.pads.targetHandler();
    for (const auto& [handle, brain] : m_scripted->bound()) {
        const combat::CommandId command = brain->human().record().padCommand;
        if (command == combat::command::kNone) {
            continue;
        }
        const std::array<script::Value, 3> args{script::Value(handle), script::Value(static_cast<double>(command)),
                                                script::Value(1.0)};
        m_scripts.call(handler, args);
    }
}

void GameplayMode::wireHub() {
    // The categories (`CfgChar` `+0x11b`) and the gang kinds' flee percentages (`CfgGang`'s ninth argument).
    auto types = std::make_shared<characters::CharacterTypes>(characters::CharacterTypes::fromRecorded(m_recorded));
    std::map<int, int> flee;
    for (const std::vector<script::Value>& call : m_recorded.calls("CfgGang")) {
        constexpr std::size_t kFleeArgument = 8;
        if (call.size() <= kFleeArgument) {
            continue;
        }
        const std::optional<double> gang = call[0].number();
        const std::optional<double> percent = call[kFleeArgument].number();
        if (gang && percent) {
            flee[static_cast<int>(*gang)] = static_cast<int>(*percent);
        }
    }
    ai::HubLookups lookups;
    lookups.category = [types](int type) -> std::optional<int> {
        const characters::CharacterType* found = types->find(type);
        return found != nullptr ? found->category : std::nullopt;
    };
    // NOLINTNEXTLINE(bugprone-exception-escape): moving the map in can only fail on allocation
    lookups.fleePercent = [flee = std::move(flee)](int kind) {
        const auto found = flee.find(kind);
        return found != flee.end() ? found->second : 0;
    };
    lookups.workout = &m_state.hub.workout;
    lookups.inBox = [this](double box, anim::Vec3 point) {
        const world_objects::VolumeBox* found = m_context.boxes != nullptr ? m_context.boxes->find(box) : nullptr;
        return found != nullptr && world_objects::VolumeBoxes::inside(*found, {point.x, point.y, point.z});
    };
    lookups.flagsInBox = [this](double box) {
        std::vector<anim::Vec3> inside;
        const world_objects::VolumeBox* found = m_context.boxes != nullptr ? m_context.boxes->find(box) : nullptr;
        if (found == nullptr) {
            return inside;
        }
        for (const world_objects::WorldFlag& flag : m_flags.all()) {
            if (world_objects::VolumeBoxes::inside(*found, flag.position)) {
                inside.push_back(anim::Vec3{flag.position[0], flag.position[1], flag.position[2]});
            }
        }
        return inside;
    };
    lookups.crimeCount = [this] { return m_state.player.crimes.reports(); };
    lookups.lastCrime = [this]() -> std::optional<anim::Vec3> {
        const std::optional<CrimePosition>& at = m_state.player.crimes.lastPosition();
        return at ? std::optional<anim::Vec3>(anim::Vec3{(*at)[0], (*at)[1], (*at)[2]}) : std::nullopt;
    };
    lookups.reportBreakIn = [this](anim::Vec3 at) {
        m_state.player.crimes.report(m_objectServices.crimeServices(), crime::kBreakAndEnter, {at.x, at.y, at.z}, 0.0,
                                     0.0, true, 0, m_scripts.now());
    };
    lookups.crimeScene = [this]() -> std::optional<anim::Vec3> {
        const std::optional<double> scene = m_flags.findByName("CrimeScene");
        const std::optional<std::array<float, 3>> at = scene ? objectPosition(*scene) : std::nullopt;
        return at ? std::optional<anim::Vec3>(anim::Vec3{(*at)[0], (*at)[1], (*at)[2]}) : std::nullopt;
    };
    m_scripted->hubHost().setLookups(std::move(lookups));
}

std::optional<std::array<float, 3>> GameplayMode::objectPosition(double handle) const {
    if (m_scripted) {
        if (const std::optional<world_objects::Placement> human = m_scripted->humanPlacement(handle)) {
            return human->position;
        }
    }
    if (const world_objects::WorldFlag* flag = m_flags.find(handle); flag != nullptr) {
        return world_objects::WorldFlags::position(
            *flag, [this](double parent) { return m_scripted ? m_scripted->humanPlacement(parent) : std::nullopt; });
    }
    if (m_context.spawnRecords != nullptr) {
        if (const world_objects::SpawnRecord* record = m_context.spawnRecords->find(handle);
            record != nullptr && !record->removed) {
            return record->position;
        }
    }
    return std::nullopt;
}

std::optional<anim::Vec3> GameplayMode::promptObjectPosition(double handle) const {
    // A tag spot is a particle system (`part_spray_tag`).
    if (m_effects) {
        if (const effects::ParticleSystem* system = m_effects->particles.find(handle)) {
            return system->position;
        }
    }
    if (const world_objects::WorldFlag* flag = m_flags.find(handle); flag != nullptr) {
        const std::array<float, 3> p = world_objects::WorldFlags::position(
            *flag, [this](double parent) { return m_scripted ? m_scripted->humanPlacement(parent) : std::nullopt; });
        return anim::Vec3{p[0], p[1], p[2]};
    }
    return std::nullopt;
}

std::optional<anim::Vec3> GameplayMode::playerFeet() const {
    const HumanCreation* player = m_humans.player(1);
    if (player == nullptr || !m_scripted) {
        return std::nullopt;
    }
    const std::optional<world_objects::Placement> placement = m_scripted->humanPlacement(player->handle);
    if (!placement) {
        return std::nullopt;
    }
    return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
}

void GameplayMode::updateActionPrompt() {
    if (m_context.hud == nullptr || !m_pickups) {
        return;
    }
    // Nothing while he sprays or plays a part in a scene; else the action object's text.
    std::string text;
    if (const std::optional<anim::Vec3> feet = playerFeet(); feet && !m_tagSession && !m_uncuff && !playerInScene()) {
        // The kinds in order: a cuffed human (0) first, then the action object.
        text = uncuffPrompt();
        if (const std::optional<ActionObject> object = m_pickups->actionObject(*feet); text.empty() && object) {
            text = object->prompt;
        }
    }
    // Only a change is written, so a prompt set some other way (the debug menu) stays until the choice changes.
    if (text != m_shownPrompt) {
        m_shownPrompt = text;
        m_context.hud->setActionPrompt(0, std::move(text));
    }
}

void GameplayMode::render(const RenderTime& time) {
    // The loading screen's tick, on game time.
    if (m_phase != Phase::Playing && m_loadingScreen != nullptr) {
        m_loadingScreen->render(time.gameTicks / (GameTimer::kTicksPerSecond / 1000));
        return;
    }
    // A screen over play is drawn over the level's frame.
    if (std::ranges::any_of(m_overlays, [](const PlayOverlay* overlay) { return overlay->showing(); })) {
        renderWithOverlay(time, [this](graphics::RenderDevice& device) {
            for (PlayOverlay* overlay : m_overlays) {
                if (overlay->showing()) {
                    overlay->draw(device);
                }
            }
        });
        return;
    }
    if (m_level) {
        m_level->render(time);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
}

void GameplayMode::renderWithOverlay(const RenderTime& time,
                                     const std::function<void(graphics::RenderDevice&)>& overlay) {
    if (auto* overlaid = dynamic_cast<OverlaidLevel*>(m_level.get())) {
        overlaid->renderWithOverlay(time, overlay);
        return;
    }
    if (m_level) {
        m_level->render(time);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    overlay(m_device);
    m_device.present();
}

void GameplayMode::exit() {
    // Left behind the loading screen: its sounds stop and it ends.
    if (m_loadingScreen != nullptr && m_phase != Phase::Playing) {
        m_loadingScreen->stopSounds();
        m_loadingScreen->end();
    }
    m_phase = Phase::Playing;
    if (m_level) {
        m_level->exit();
    }
    // Mode 1's exit stops the sounds and music; the level's emitters and lines go with it.
    if (m_context.sound != nullptr) {
        m_context.sound->gameplayLeft();
    }
    endLevel();
    // The screens over play end with the level.
    for (PlayOverlay* overlay : m_overlays) {
        overlay->levelEnded();
    }
    m_humans.clear();
    m_flags.clear();
    if (m_context.messages != nullptr) {
        m_context.messages->clear();
    }
    if (m_context.boxes != nullptr) {
        m_context.boxes->clear();
    }
    // UnloadLevel destroys the script system and makes it again: the next level starts from the bindings alone.
    if (m_scripts.exists()) {
        m_scripts.create();
    }
}

void GameplayMode::suspend() {
    // A mode pushed over play (the pause menu) hides the HUD under it (docs/research/hud.md#who-shows-the-hud-again).
    // The flag 0x005e5580 that skips this is not traced, and there is no Armies of the Night level in Coney yet.
    if (m_context.hud != nullptr) {
        m_context.hud->hideAll();
    }
    if (m_level) {
        m_level->suspend();
    }
}

void GameplayMode::resume() {
    // Play resuming shows the HUD, even one a script hid, unless player 1 is in a scene. The original also needs
    // game state +0x14c at 0 (no level change asked for); Coney leaves play at once instead of setting it.
    if (m_context.hud != nullptr && !m_context.hud->visible() && !playerInScene()) {
        m_context.hud->showAll();
    }
    if (m_level) {
        m_level->resume();
    }
}

bool GameplayMode::playerInScene() const {
    const HumanCreation* player = m_humans.player(1);
    return player != nullptr && m_scenes && m_scenes->inScene(player->handle);
}

} // namespace coney
