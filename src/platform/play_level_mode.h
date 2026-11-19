// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/dealer_goal.h"
#include "ai/route_planner.h"
#include "animation/anim_math.h"
#include "camera/camera_lens.h"
#include "characters/character_choice.h"
#include "characters/character_types.h"
#include "characters/dynamic_clips.h"
#include "combat/stick.h"
#include "combat/throw_aim.h"
#include "core/error.h"
#include "core/interpolation.h"
#include "core/options.h"
#include "effects/glints.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/play_level_simulation.h"
#include "graphics/lighting_switches.h"
#include "graphics/render_device.h"
#include "graphics/throw_arc.h"
#include "gui/pause_menu/pause_stats.h"
#include "hud/health_rings.h"
#include "human/place.h"
#include "human/player.h"
#include "human/strike_shapes.h"
#include "human/target_human.h"
#include "platform/blood_textures.h"
#include "platform/character_mesh.h"
#include "platform/hud_layer.h"
#include "platform/humans_trace.h"
#include "platform/mesh_view_hook.h"
#include "platform/placed_objects.h"
#include "platform/play_lighting.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/scene_stage.h"
#include "platform/texture_dictionary.h"
#include "platform/throw_arc_renderer.h"
#include "platform/world_renderer.h"
#include "warriors/created_humans.h"
#include "world/debug_camera.h"
#include "world/sector_budget.h"
#include "world_objects/cars.h"
#include "world_objects/drawn_poses.h"
#include "world_objects/hats.h"
#include "world_objects/level_objects.h"
#include "world_objects/lock_pick.h"
#include "world_objects/loose_objects.h"
#include "world_objects/object_bodies.h"
#include "world_objects/object_list.h"
#include "world_objects/object_tasks.h"
#include "world_objects/pickups.h"
#include "world_objects/spinning_icons.h"

namespace rw {
struct Texture;
}

namespace coney::scenes {
class SceneList;
class SceneSystem;
} // namespace coney::scenes

namespace coney::script {
class SoundHost;
} // namespace coney::script

namespace coney::platform {

class PlayLevelEffects;

/// Who the play mode's player is and how he is first placed, beyond where (the scenery's start).
struct PlayerSetup {
    /// The Character List model the player is drawn and animated as (the level script's type, through `CfgChar`,
    /// characters::modelNameFor()). **Coney's choice:** when it is empty or fails to load, Rembrandt's, with a line in
    /// the log.
    std::string model{human::kPlayerModel};
    /// The character type the player was made as (`HuCreate`'s type): his class (`types`) and the Player page.
    int type = human::kPlayerType;
    /// The character types the scripts configured (`CfgChar`, with the power and Warrior classes): the player takes
    /// his class from them (human::playerClassOf()), and the debug menus can rebuild him as any of them
    /// (changeCharacter()); empty: none, and the player plays his files' own damage.
    characters::CharacterTypes types;
    /// Whether the start is snapped to the ground as `HuCreate` does; false for a start a `TeleportToFlag` gave, which
    /// does not snap (docs/research/flags.md#position).
    bool snapToGround = true;
    /// What the AI fighters are (ai::aiConfigFrom() of the scripts' configuration calls); the research's reference
    /// values by default.
    ai::AiConfig ai;
};

class PlayLevelMode;

/// A line PlayLevelMode draws into the scene (PlayLevelMode::setSceneLines()): its ends in the game's axes and its
/// colour.
struct SceneLine {
    anim::Vec3 a;
    anim::Vec3 b;
    graphics::Rgba colour;
};

/// The lines to draw this frame, from the mode, player 1's snapshot as drawn and the frame's alpha: the debug tools'
/// Debug draw page (PlayLevelControls::sceneLines()).
using SceneLines = std::function<std::vector<SceneLine>(const PlayLevelMode& mode,
                                                        const human::PlayerSnapshot& snapshot, float alpha)>;

/// Coney's first playable mode, behind `coney --play-level NAME` (docs/guides/running-play-level.md#playing-a-level):
/// Rembrandt standing in a scenery (PlayScenery) at its start, driven by pad 1's left stick, and the follow camera
/// behind him turned by the right stick. The scenery is a level's streamed worlds and level file, loaded as the world
/// viewer loads them and streamed around the follow camera, or a sandbox layout (`--play-level sandbox:NAME`,
/// docs/guides/sandbox.md): the same player and camera code on the sandbox's collision mesh.
///
/// Every step (PlayLevelSimulation::update()): the player's update (human::Player: the human, then the camera), then
/// the scenery's step this class makes through the step's hooks (for a level: one streaming decision, the draw
/// distance, and the visibility pass the next step's streaming reads).
/// Every real frame (render()): the drawing, from the player's snapshots of the last two steps blended by the frame's
/// alpha (human::interpolate()) and the draw distance blended the same way: the character skinned and drawn with the
/// scenery, where the original draws its objects (docs/guides/conventions.md#update-and-render). Game time only, so
/// with `--frames` and `--input-script` a run is the same every time, and the simulation is the same at any frame rate.
///
/// A level entered through GameplayMode (the story, `--play-level LEVEL`) brings its scripts' cast (ScriptedCast):
/// player 1 is the scripts' first player, and every other human the scripts create is an AI human (ai::AiHumans on
/// the level's brains) drawn with its own model (src/gamemodes/play_level_cast.cpp). Otherwise the other characters are
/// a sandbox layout's `target` lines, Coney's passive targets (human::TargetHuman) stepped after the player, and AI
/// fighters (a layout's `fighter` lines, or spawned from the debug menus), humans with a brain stepped in the player's
/// characters' step; both are drawn with the player's model. The parts follow docs/research/characters.md,
/// docs/research/combat/index.md, docs/research/ai/index.md and docs/research/camera.md; the mode is Coney's own glue.
///
/// A level entered through gameplay also brings its glass panes and doors (ScriptedCast::objects,
/// src/gamemodes/play_level_objects.cpp): they get the level's collision mesh and path data, tick twice a step, take
/// player 1's landed hits, and triangle at a pickable door starts a lock pick that takes the pad until it ends.
///
/// What it simulates (the cast and the fighters, the objects, the hats, the carried props, the throw aim, the HUD's
/// state) is its base, PlayLevelSimulation (src/gamemodes/play_level_simulation.h), which needs neither librw nor SDL;
/// this class adds the drawing (the meshes per human, the textures, the lights, the world objects' models, the HUD's
/// sprites, the throw arc), the scenery's streaming and the scenes' stage. The step is the simulation's; this class
/// makes the parts of it the simulation leaves to a presentation through its hooks (stepScenery(), stepGlints(),
/// hudStepped(), stepRings() and the rest; docs/guides/network-architecture.md#sub-presentation).
///
/// It also has Coney's own capabilities, which the original has nothing like (docs/guides/conventions.md#coney-only):
/// teleporting, freezing and flying the player, the free camera, spawning fighters and objects, changing his character.
/// The debug menus reach them, and the parts that are debug tooling alone (the Entity inspector, the Animation player,
/// the Debug draw page's lines), through PlayLevelControls in the debug tools (src/platform/play_level_controls.h,
/// docs/guides/building.md#debug-tools); render() draws the lines a hook gives.
class PlayLevelMode final : public PlayLevelSimulation, public OverlaidLevel {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x106;

    /// Loads level `name` (LevelPlayScenery::load()) and the player's character from `wad`, the player at `start`
    /// (player 1 as the level script created him) when given, as `setup` says. With `cast` (the level's scripts' humans
    /// and brains, from gameplay), the humans the scripts create are made AI humans in the scene (makeCast()).
    /// `print` receives what was loaded and streamed (counts only). Everything given must outlive the mode. Fails as
    /// the loaders do.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
           std::function<void(std::string_view)> print, std::optional<human::PlayerStart> start = std::nullopt,
           const PlayerSetup& setup = {}, const ScriptedCast* cast = nullptr);

    /// The mode round `scenery` (a level's, or another the caller made: the debug tools' sandbox,
    /// createPlayInSandbox()), with the character and its texture loaded from `wad` as `setup` says. Fails as the
    /// character loader does.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    createWith(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
               std::function<void(std::string_view)> print, const PlayerSetup& setup = {},
               const ScriptedCast* cast = nullptr);

    ~PlayLevelMode() override;
    PlayLevelMode(const PlayLevelMode&) = delete;
    PlayLevelMode& operator=(const PlayLevelMode&) = delete;
    PlayLevelMode(PlayLevelMode&&) = delete;
    PlayLevelMode& operator=(PlayLevelMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// Preloads the scenery around the camera.
    void enter() override;
    /// Draws a frame between the last two steps and presents it.
    void render(const RenderTime& time) override;
    /// render() with `overlay` drawn over the HUD before the present: the pause menu over the paused level, the Rumble
    /// intro and result screen over play.
    void renderWithOverlay(const RenderTime& time,
                           const std::function<void(graphics::RenderDevice&)>& overlay) override;
    /// renderWithOverlay() without the HUD: the Armies game over's world under its menu.
    void renderWorldWithOverlay(const RenderTime& time,
                                const std::function<void(graphics::RenderDevice&)>& overlay) override;
    /// One line: frames, the player's position, heading, speed, gait, clip and state, then the scenery's counts.
    [[nodiscard]] std::string summary() const;

    /// Writes the trace (human::traceHeader(), then human::traceLine() after every step) to the file at `path`,
    /// replacing it: `--trace`. Fails with ErrorCode::Io when the file cannot be opened.
    [[nodiscard]] std::expected<void, Error> traceTo(const std::string& path);
    /// Writes every human's row (ai::FightTracer) to `trace` after every step: `--trace-humans`. The file is shared, so
    /// a run that loads several levels writes one.
    void traceHumansTo(std::shared_ptr<HumansTrace> trace) { m_humansTrace = std::move(trace); }

    /// Starts the player at `place` instead (dropped onto the ground below it, as a start is), and the camera where it
    /// says, if it says: `--start`, Coney's own test aid.
    void startAt(const StartPlace& place);

    /// Pins player 1's view at `pin` for the rest of the run, whatever camera the game shows (scene cameras too), with
    /// the current camera's near and far clips: `--camera`, Coney's test aid for matching the original's frames. The
    /// scenery streams and culls round the pinned view.
    void pinCamera(const CameraPin& pin);
    /// Freezes the world after the next step: from then on a step only streams the scenery round the camera and brings
    /// in the world objects there, and every frame is drawn from that step with the scenery's animations held, so each
    /// frame shows the same picture: `--freeze-world`.
    void freezeWorld() { m_freezeWorld = true; }
    /// Whether the world is frozen now (freezeWorld() and its first step done).
    [[nodiscard]] bool worldFrozen() const { return m_freezeWorld && m_frozenSince.has_value(); }

    /// Sets the lines render() draws into the scene each frame, through the camera and tested against depth (the debug
    /// tools' Debug draw page, PlayLevelControls::sceneLines()); empty draws none.
    void setSceneLines(SceneLines lines) { m_sceneLines = std::move(lines); }
    /// Sets the debug tools' mesh views (docs/guides/debug-menu.md#mesh-views), which render() asks at each frame's
    /// start and, while they are on, hands every model the scene draws (MeshViewHook); null: none, and every model is
    /// drawn as the game draws it.
    /// @coney-only the debug tools' mesh views; the original has none
    void setMeshViews(std::shared_ptr<MeshViewHook> views) { m_meshViews = std::move(views); }
    /// Sets the lighting lab's drawing-side switches (docs/guides/realtime-lighting.md#lab), which must outlive the
    /// mode; null (the default): the faithful picture, lit from the camera drawn through.
    /// @coney-only the debug tools' lighting lab; the original has none
    void setLightingSwitches(const graphics::LightingSwitches* switches) { m_lightingSwitches = switches; }
    /// Changes the scenery through the debug tools' world edits (PlayScenery::setEditHook(): `--world-edit`); null:
    /// none.
    /// @coney-only the debug tools' world edits; the original has none
    void setWorldEdits(const std::shared_ptr<WorldEditHook>& edits) { m_playScenery.setEditHook(edits); }

    // Coney's own capabilities and views of the play: the debug menus' Player, Camera, Spawner and AI fighters pages
    // reach them through PlayLevelControls, and the disc tests call them.
    /// Player 1's feet, in the game's axes.
    [[nodiscard]] anim::Vec3 playerFeet() const;
    /// Player 1's heading in degrees (0 faces +y).
    [[nodiscard]] float playerHeadingDegrees() const;
    /// Player 1's speed, metres a second.
    [[nodiscard]] float playerSpeed() const;
    /// One line of how player 1 moves: gait, clip, ground or air, traversal, stamina.
    [[nodiscard]] std::string playerState() const;
    /// Puts player 1 at `place`, dropped onto the ground below it, with the camera behind him.
    void teleport(const human::Place& place);
    /// The scene's places: a level's player start, a sandbox's spawn points.
    [[nodiscard]] std::vector<human::Place> places() const { return m_scenery->places(); }
    /// Whether player 1 is frozen (he ignores the pad).
    [[nodiscard]] bool playerFrozen() const { return m_frozen; }
    /// Freezes or frees player 1.
    void setPlayerFrozen(bool frozen) { m_frozen = frozen; }
    /// Whether player 1 flies through everything (human::Human::setFlight(), the noclip).
    [[nodiscard]] bool noclip() const { return m_player->noclip(); }
    /// Switches the noclip.
    void setNoclip(bool on) { m_player->setNoclip(on); }
    /// The follow camera's eye, in the game's axes.
    [[nodiscard]] anim::Vec3 cameraEye() const;
    /// The point the follow camera looks at, in the game's axes.
    [[nodiscard]] anim::Vec3 cameraTarget() const;
    /// Places the follow camera behind player 1 again, with the current follow settings.
    void resetCamera() { m_player->resetCamera(); }
    /// Whether the free camera flies with pad 1 (the player stands still).
    [[nodiscard]] bool freeCamera() const { return m_freeCamera.has_value(); }
    /// Switches the free camera, starting where the follow camera is.
    void setFreeCamera(bool on);
    /// The collision mesh the player and the AI stand on: the level's or the sandbox's, with any spawned objects in it.
    [[nodiscard]] const raycast::CollisionMesh& collision() const { return m_scenery->collision(); }
    /// Whether objects can be spawned into the scenery (spawn()).
    [[nodiscard]] bool canSpawn() const { return m_playScenery.canSpawn(); }
    /// Puts `extras` in the scenery and its collision in place of those added before (the debug tools' Spawner,
    /// PlayLevelControls::spawn()); empty: the scenery as made. Fails as PlayScenery::setExtras() does.
    std::expected<void, Error> setSceneExtras(SceneExtras extras) {
        return m_playScenery.setExtras(m_engine, std::move(extras));
    }
    /// The objects added to the scenery now.
    [[nodiscard]] const SceneExtras& sceneExtras() const { return m_playScenery.extras(); }
    /// How many objects were added to the scenery.
    [[nodiscard]] std::size_t spawnedCount() const { return m_playScenery.extras().count; }
    /// Makes an AI fighter at `feet` facing `headingDegrees`, drawn as the scene's character.
    std::expected<void, Error> spawnFighter(anim::Vec3 feet, float headingDegrees);
    /// The models a fighter can be drawn as: the distinct models of characterChoices(), sorted.
    [[nodiscard]] std::vector<std::string> fighterModels() const;
    /// Makes an AI fighter drawn as `model`, on `side` (ai::FighterSides::join() says which gang that puts it in).
    std::expected<void, Error> spawnFighterAs(anim::Vec3 feet, float headingDegrees, std::string_view model,
                                              ai::FighterSide side = ai::FighterSide::Enemy);
    /// Removes the AI fighters the debug menus spawned (spawnFighter(), spawnFighterAs()), through the game's removal;
    /// the player, his crew, the level's scripts' humans and the layout's fighters stay.
    /// @coney-only debug tooling: the original has no spawner
    void clearFighters();
    /// Whether idle fighters take the player on.
    [[nodiscard]] bool fightersEngage() const { return m_ai->engaging(); }
    /// Lets idle fighters take the player on, or keeps them from it.
    void setFightersEngage(bool on) { m_ai->setEngaging(on); }
    /// One line of the fighters' state.
    [[nodiscard]] std::string fightersState() const;
    /// One line of the player's character and classes.
    [[nodiscard]] std::string characterState() const;
    /// What the pause menu's Stats screen reads of player 1 (docs/research/pause.md#stats-screen): his type, his
    /// Warrior class's damage scale, his power class's byte `+0x40` and his power, stamina and rage maxima. **Coney's
    /// choice**: a type with no recorded Warrior class or power class gives the player's defaults, as he plays with.
    [[nodiscard]] gui::WarriorStats warriorStats() const;
    /// Rebuilds the player as `type` (the debug menus' Change character): the model a player of the type is drawn
    /// as and its files (the anim set, the moves' Anim Range List, the speeds its clips give), loaded as the mode's
    /// start loads them, and his class as the start's player takes it (human::playerClassOf(): the class's damage
    /// scaled by his Warrior class, his power class), at full health where he stands. The AI
    /// fighters are made again where they stand, at full health, since their brains hold the player they fought. In a
    /// level whose scripts drive the cast the brains and scripts hold the player, so he becomes the type in place
    /// (human::Player::changeCharacter(): the same files and class, at full health) and the scripts' record of him
    /// and his brain take the new type; the cast is left as it is.
    std::expected<void, Error> changeCharacter(int type);

    /// Plays the game's scenes in this mode (src/platform/play_level_scene.cpp): `scenes` is stepped with every step
    /// and drawn through the mode's SceneStage (its camera, letterbox, fades and the bound humans), `playerHandle` is
    /// player 1's script handle, whom a scene drives as the mode's own player. Null detaches. `scenes` must outlive the
    /// attachment.
    void attachScenes(scenes::SceneSystem* scenes, double playerHandle);
    /// `--scene NAME`, Coney's test aid: plays the scene at once as a cinematic, as global.lua's gPlayCutScene would,
    /// with stand-ins of its roles' characters bound to its roles and player 1 to his (a role named for Rembrandt).
    /// Fails when the scene list cannot be read or the scene does not load.
    std::expected<void, Error> playScene(std::string_view name);
    /// Plays the scenes' sounds through `sounds` (null: none); it must outlive the mode's use of it.
    void setSounds(audio::SoundPlayer* sounds) { m_stage->setSounds(sounds); }
    /// The scene stage: what the playing scene shows.
    [[nodiscard]] const SceneStage& stage() const { return *m_stage; }

    /// Poses `mesh` as `character` in `pose`, leaned by `lean` and turned to `heading` at `feet`, in the world
    /// (RenderWare's axes) for drawing (CharacterMesh::pose()), the body `scale` times its size (1 unless the Entity
    /// editor scaled it: Human::meshScale()).
    static void skin(const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet, float heading,
                     float lean, CharacterMesh& mesh, float scale = 1.0F);

    /// ScriptedPlayer: a script's teleport of player 1 during play: placed (dropped onto the ground at most 0.5 m
    /// below, unless a scene holds him) with his health, meters and clip kept, his brain's front move popped, and for
    /// `TeleportToFlag` his move stopped (human::Player::teleport(); `Teleport`: human::Player::place()).
    void teleportPlayer(const world_objects::Placement& placement, world_objects::TeleportMotion motion) override;
    using ScriptedPlayer::teleportPlayer;
    /// ScriptedPlayer: player 1's spray clips for a tag at `point`.
    bool startTagSpray(const std::array<float, 3>& point) override;
    /// ScriptedPlayer: player 1's held object falls from his hand as a spray or an uncuff begins.
    void dropHeldObject() override;
    /// ScriptedPlayer: the type name of the object in player 1's hand.
    [[nodiscard]] std::string heldObjectType() const override;
    /// ScriptedPlayer: whether player 1's spray loop plays.
    [[nodiscard]] bool tagSprayLooping() const override;
    /// ScriptedPlayer: whether player 1's spray clips still play.
    [[nodiscard]] bool tagSprayPlaying() const override;
    /// ScriptedPlayer: player 1's spray clips restart (337, then the loop) as a pause begins.
    void restartTagSpray() override;
    /// ScriptedPlayer: player 1's spray clips end.
    void endTagSpray() override;

    /// How many HUD sprites the newest step queued (HudLayer::spritesQueued()).
    [[nodiscard]] std::size_t hudSprites() const { return m_hudLayer->spritesQueued(); }
    /// The glints of the pickups and lock-pickable doors the newest step keeps (docs/research/particles.md#glints).
    [[nodiscard]] const effects::Triglints& glints() const { return m_glints; }
    /// The health rings and L1 markers the newest step queued.
    [[nodiscard]] const hud::HealthRings& healthRings() const { return m_rings; }
    /// How many world objects the last frame drew (0 without pixels).
    [[nodiscard]] std::size_t objectsDrawn() const { return m_placed ? m_placed->drawn() : 0; }

  private:
    // A character's resources and its texture dictionaries, ready to draw.
    struct LoadedCharacter {
        std::unique_ptr<human::PlayerCharacter> character;
        std::vector<TextureDictionary> dictionaries;
    };

    PlayLevelMode(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
                  LoadedCharacter loaded, std::function<void(std::string_view)> print, std::string model,
                  const PlayerSetup& setup, const ScriptedCast* cast, std::unique_ptr<PlayLevelEffects> levelEffects);

    // Loads the character `model` names from `wad` and its textures, converted for drawing when `engine` draws: the
    // player's creation, at the start and at a change of character (src/platform/play_level_character.cpp).
    [[nodiscard]] static std::expected<LoadedCharacter, Error> loadCharacter(RenderEngine& engine, const io::Wad& wad,
                                                                             std::string_view model);
    // Draws the player as `loaded` (the model `model`) from now on and keeps its files: a hand-over keeps his
    // animations and class; a change of character in a scripted level rebuilds him as `loaded` first.
    void swapPlayerModel(LoadedCharacter loaded, const std::string& model);
    // The texture a character's dictionaries hold, which every material uses; null when none.
    [[nodiscard]] static rw::Texture* textureOf(const std::vector<TextureDictionary>& dictionaries);

    // The camera of `snapshot` as the scenery draws it, in RenderWare's axes, with `drawDistance` as its far clip.
    [[nodiscard]] WorldView view(const human::PlayerSnapshot& snapshot, float drawDistance) const;
    // The view a step or a frame draws through: the pinned camera, else a scene's camera, else the free camera, else
    // the follow camera of `snapshot`; `free` is the free camera's pose to use when it is on.
    [[nodiscard]] WorldView chosenView(const human::PlayerSnapshot& snapshot, float drawDistance, float alpha,
                                       const world::CameraPose* free) const;
    // The scenery's part of a step from the camera chosenView() gives (streaming, then the draw distance), returning
    // that view at the new draw distance.
    WorldView stepSceneryView(const FrameTime& frame);
    // A step of the frozen world: the scenery's streaming and visibility round the camera and the world objects
    // brought in there, nothing advanced.
    void stepFrozen(const FrameTime& frame);

    // The presentation's hooks (PlayLevelSimulation): the frozen world's step; the traces; the scenery's streaming
    // round the step's camera, the lights' step and the visibility pass; the HUD's sprites and the spinning icons'
    // poses; the step's end (`--freeze-world` freezes here); and the scenes' three until they move to the core.
    bool stepFrozenWorld(const FrameTime& frame) override;
    void traceStep() override;
    StepView stepScenery(const FrameTime& frame) override;
    void hudStepped() override;
    void stepEnded(const FrameTime& frame) override;
    [[nodiscard]] bool sceneHolds(double human) const override;
    [[nodiscard]] bool letterboxShown(std::uint64_t nowMs) const override;
    // Makes the scene stage (play_level_scene.cpp).
    void makeStage();
    // One step of the scenes at `nowMs` with pad 1's and 2's buttons (a released human is placed as it is let go,
    // by placeReleased).
    void stepScenes(std::uint64_t nowMs, std::uint16_t buttons) override;
    // Places a human the scene has let go, where the scene left it.
    void placeReleased(const SceneStage::Release& release);
    // The ground a placement of `human` drops onto (human::Human::place()): the level's collision, or null while a
    // scene holds the human.
    [[nodiscard]] const raycast::CollisionMesh* placeGround(double human) const override;
    // The brain's part of a placement of `human` (ai::Brain::placed(): its front move popped), when it has a brain.
    void brainPlaced(const human::Human& human);
    // A character to draw and animate a human as, and its texture.
    struct CastLook {
        const human::PlayerCharacter* character = nullptr;
        rw::Texture* texture = nullptr;
    };
    // A character for a human of `model` to be drawn as: the one the simulation plays (castCharacter()) and the
    // texture its dictionaries hold; the scene's character and texture when it is empty or did not load.
    CastLook castLook(const std::string& model);
    // The simulation's hooks (PlayLevelSimulation): the character `model` names, loaded with its textures (kept for
    // drawing by model); a mesh for the AI human made; the player drawn as `model` (HuChangePlayerGang's hand-over).
    [[nodiscard]] std::expected<std::unique_ptr<human::PlayerCharacter>, Error>
    loadCastCharacter(std::string_view model) override;
    void humanMade(const human::Human& human, const std::string& model) override;
    void changePlayerModel(const std::string& model) override;

    // --- play_level_world.cpp: the throw's arc and the glass panes, drawn.

    // Draws player 1's throw-aim arc while he aims, `alpha` of the way from the step before's arc to the newest, red
    // while it meets a target (docs/research/graphics.md#throw-arc).
    // @orig 0x00419da0 ThrowArcs_RenderAll (unknown)
    void drawThrowArc(float alpha);
    // The level's glass panes, after the opaque world and before the rings (docs/research/objects.md#pane-draw).
    void drawGlass() const;

    // --- play_level_world.cpp: the world objects and the health rings, and what the humans hold, wear and carry,
    // drawn (the simulation keeps them: src/gamemodes/play_level_simulation.h).

    // Loads the Object List the world objects' models come by (only when the engine draws).
    void makeWorldObjects(const ScriptedCast& cast);
    // The glints' step (docs/research/particles.md#glints): a triglint for each `pickup_item` lying in the world and
    // each lock-pickable door, then `ticks` 60 Hz ticks of their blink, a point seen when it is inside the step's view
    // window (m_stepView) within the triglint's distance.
    void stepGlints(int ticks) override;
    // The health rings' step: player 1 and his target, the triggers from `pad`, the camera's heading from the step's
    // view (m_stepView).
    void stepRings(const Pad& pad, std::uint64_t nowMs) override;
    // Records where this step's world objects and doors are, for render() to blend from (after stepWorldObjects()).
    void recordDrawnPoses() override;
    // One spinning icon drawn this step: the human wearing it, its model's name and its pose.
    struct IconDraw {
        const human::Human* human = nullptr;
        std::string name;
        world_objects::IconPose pose;
    };
    // The spinning icons over the humans that wear one, as of the HUD's newest step.
    [[nodiscard]] std::vector<IconDraw> iconDraws() const;
    // Records where this step's spinning icons are, for render() to blend from (after the HUD's step).
    void recordIconPoses();
    // Draws the world objects (lit as objects) `alpha` of the way from the step before to the newest, then what the
    // humans hold and wear (drawAttachedObjects()).
    void drawWorldObjects(float alpha);
    // Queues the object in `human`'s hand for this frame's draw (drawAttachedObjects()), posed as heldDrawOf() says.
    void poseHeldObject(const human::Human& human, const human::PlayerCharacter& character, const anim::Pose& pose,
                        anim::Vec3 feet, float heading);
    // Draws this step's health rings and L1 markers, each under where its human is drawn (`feet` by ring id).
    void drawRings(const std::map<std::uint64_t, anim::Vec3>& feet) const;

    // Notes where `human`'s hat is drawn this frame, `character` posed by `pose` at `feet` facing `heading`.
    void poseHat(const human::Human& human, const human::PlayerCharacter& character, const anim::Pose& pose,
                 anim::Vec3 feet, float heading);
    // Places this frame's worn hats and carried props among the world objects (before PlacedObjects::draw()).
    void drawAttachedObjects();

    // Notes where `human`'s carried prop is drawn this frame, `character` posed by `pose` at `feet` facing `heading`.
    void poseCarriedProp(const human::Human& human, const human::PlayerCharacter& character, const anim::Pose& pose,
                         anim::Vec3 feet, float heading);

    // Draws the humans: their lights, the render states, each mesh in its two passes.
    void drawCharacter() const;
    // Draws one human's passes (PlayLighting::drawHumanPasses), with his blood layer marked for the mesh views and the
    // passes drawn recorded on his mesh for the Mesh group; the game's draw itself is untouched.
    void drawHumanPasses(CharacterMesh& mesh, bool player, rw::Texture* blood) const;
    // The blood texture of the second pass of a human with `health`, or null when it shows none.
    [[nodiscard]] rw::Texture* bloodTextureFor(const combat::Health& health) const;
    // The view from a camera pose (RenderWare's axes) through the player camera's lens, with `drawDistance`.
    [[nodiscard]] WorldView viewFrom(const world::CameraPose& pose, float drawDistance,
                                     const camera::CameraLens& lens = camera::kPlayerCameraLens) const;
    // The free camera `camera` between its last two steps, `alpha` of the way.
    [[nodiscard]] static world::DebugCamera blendedFreeCamera(const Interpolated<world::DebugCamera>& camera,
                                                              float alpha);
    // Draws the lines of the setSceneLines() hook through the current camera, from `snapshot`, the models at `alpha`
    // of a step.
    void drawSceneLines(const human::PlayerSnapshot& snapshot, float alpha) const;
    // The debug tools' view of the mode (src/platform/play_level_controls.h): it reads the mode and sets the state
    // below that only debug tooling uses (the Animation player's sets, the frozen humans).
    friend class PlayLevelControls;

    RenderEngine& m_engine;
    // The simulation's scenery (PlayLevelSimulation::m_scenery, which owns it), with its streaming and drawing.
    PlayScenery& m_playScenery;
    std::vector<TextureDictionary> m_dictionaries; // before the mesh, which holds a reference to their texture
    // The humans' shared blood textures, before every mesh, whose dual layers hold references to them.
    std::optional<BloodTextures> m_blood;
    // After a change of character, the textures of the player's own character (PlayLevelSimulation::m_playerCharacter).
    std::vector<TextureDictionary> m_playerDictionaries;
    std::unique_ptr<CharacterMesh> m_mesh;
    std::unique_ptr<PlayLighting> m_lights;    // the level's lights (the cast's), or a stand-in
    Interpolated<float> m_drawDistance;        // at the last two steps
    WorldView m_stepView;                      // the newest step's view, which its glints and rings are seen through
    SceneLines m_sceneLines;                   // setSceneLines()
    std::shared_ptr<MeshViewHook> m_meshViews; // setMeshViews()
    const graphics::LightingSwitches* m_lightingSwitches = nullptr; // setLightingSwitches()
    std::optional<CameraPin> m_pinnedCamera;                        // `--camera`
    bool m_freezeWorld = false;                                     // `--freeze-world`
    std::optional<std::uint64_t> m_frozenSince;                     // the game ticks of the step the world froze after
    // The sandbox's targets' meshes (PlayLevelSimulation::m_targets), each its own copy of the character's, by target.
    std::map<const human::TargetHuman*, std::unique_ptr<CharacterMesh>> m_targetMeshes;
    // The textures of the characters the scripts' humans and the fighters are drawn as, by model (the characters
    // themselves are the simulation's: PlayLevelSimulation::m_castCharacters); none for a model that did not load.
    std::map<std::string, std::vector<TextureDictionary>, std::less<>> m_castDictionaries;
    // The AI humans' meshes, by human (drawn in the fighters' order), each skinned as the character he plays
    // (PlayLevelSimulation::characterOf()).
    struct FighterMesh {
        std::unique_ptr<CharacterMesh> mesh;
        bool hidden = false; // its human was deleted
    };
    std::map<const human::Human*, FighterMesh> m_fighterMeshes;
    rw::Texture* m_texture = nullptr; // the character's texture, for new meshes
    std::string m_sceneModel;         // the model of the scene's character (m_character), as the mode started
    // The scenes: the stage drawn and stepped with them, the system attached (or the test aid's own) and player 1's
    // handle in them.
    std::unique_ptr<SceneStage> m_stage;
    scenes::SceneSystem* m_scenes = nullptr;
    std::unique_ptr<scenes::SceneList> m_ownSceneList;
    std::unique_ptr<scenes::SceneSystem> m_ownScenes;
    // The --trace file (closed when unset) and the steps traced.
    std::optional<std::ofstream> m_trace;
    std::uint64_t m_traceSteps = 0;
    // The --trace-humans file (shared with the other levels of the run), when asked for.
    std::shared_ptr<HumansTrace> m_humansTrace;
    // The level's parked cars, particles and motion blur (play_level_effects.h), when gameplay brought them.
    std::unique_ptr<PlayLevelEffects> m_levelEffects;
    // The world objects' and doors' poses at the last two steps, by their draw id, and the spinning icons' by their
    // human, for a render between steps (docs/research/rendering.md#interpolation).
    world_objects::DrawnPoses<double> m_drawnPoses;
    world_objects::DrawnPoses<const human::Human*> m_iconPoses;
    effects::Triglints m_glints;
    std::unique_ptr<ThrowArcRenderer> m_throwArc;            // made when the arc is first drawn
    std::unique_ptr<world_objects::ObjectList> m_objectList; // before the models that read it
    std::unique_ptr<PlacedObjects> m_placed;
    // Where this frame draws the worn hats and the carried props.
    std::vector<std::pair<double, world_objects::WorldPose>> m_attachedDraws;
    hud::HealthRings m_rings;
    std::map<std::uint64_t, int> m_ringHealth; // each ringed human's health at the last step, for its hit pulse
    std::unique_ptr<HudLayer> m_hudLayer;      // the HUD's sheets, batches and pass (src/platform/hud_layer.h)
    // The layer renderWithOverlay() adds over the HUD for the render it runs; null otherwise.
    const std::function<void(graphics::RenderDevice&)>* m_overlay = nullptr;
    // Whether the render renderWorldWithOverlay() runs leaves the HUD out.
    bool m_hudHidden = false;
};

} // namespace coney::platform
