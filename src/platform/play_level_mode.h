// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
#include "ai/route_planner.h"
#include "animation/anim_math.h"
#include "camera/camera_lens.h"
#include "characters/character_types.h"
#include "characters/dynamic_clips.h"
#include "combat/stick.h"
#include "core/error.h"
#include "core/interpolation.h"
#include "core/options.h"
#include "debug/play_controls.h"
#include "effects/glints.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/gameplay_mode.h"
#include "graphics/render_device.h"
#include "hud/health_rings.h"
#include "human/player.h"
#include "human/strike_shapes.h"
#include "human/target_human.h"
#include "platform/blood_textures.h"
#include "platform/character_mesh.h"
#include "platform/hud_layer.h"
#include "platform/placed_objects.h"
#include "platform/play_lighting.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/scene_stage.h"
#include "platform/texture_dictionary.h"
#include "platform/world_renderer.h"
#include "sandbox/sandbox_world.h"
#include "warriors/created_humans.h"
#include "world/debug_camera.h"
#include "world/sector_budget.h"
#include "world_objects/cars.h"
#include "world_objects/hats.h"
#include "world_objects/level_objects.h"
#include "world_objects/lock_pick.h"
#include "world_objects/loose_objects.h"
#include "world_objects/object_bodies.h"
#include "world_objects/object_list.h"
#include "world_objects/object_tasks.h"
#include "world_objects/pickups.h"

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
    /// (debug::PlayControls::changeCharacter()); empty: none, and the player plays his files' own damage.
    characters::CharacterTypes types;
    /// Whether the start is snapped to the ground as `HuCreate` does; false for a start a `TeleportToFlag` gave, which
    /// does not snap (docs/research/flags.md#position).
    bool snapToGround = true;
    /// What the AI fighters are (ai::aiConfigFrom() of the scripts' configuration calls); the research's reference
    /// values by default.
    ai::AiConfig ai;
};

/// What a play run has done so far: counts and the player's state, for the summary line.
struct PlayStats {
    std::uint64_t frames = 0;
    float travelled = 0.0F; ///< Metres the player's feet moved across the ground.
};

/// Coney's first playable mode, behind `coney --play-level NAME` (docs/guides/building.md#playing-a-level): Rembrandt
/// standing in a scenery (PlayScenery) at its start, driven by pad 1's left stick, and the follow camera behind him
/// turned by the right stick. The scenery is a level's streamed worlds and level file, loaded as the world viewer
/// loads them and streamed around the follow camera, or a sandbox layout (`--play-level sandbox:NAME`,
/// docs/guides/sandbox.md): the same player and camera code on the sandbox's collision mesh.
///
/// Every step (update()): the player's update (human::Player: the human, then the camera), then the scenery's step
/// (for a level: one streaming decision, the draw distance, and the visibility pass the next step's streaming reads).
/// Every real frame (render()): the drawing, from the player's snapshots of the last two steps blended by the frame's
/// alpha (human::interpolate()) and the draw distance blended the same way: the character skinned and drawn with the
/// scenery, where the original draws its objects (docs/guides/conventions.md#update-and-render). Game time only, so
/// with `--frames` and `--input-script` a run is the same every time, and the simulation is the same at any frame rate.
///
/// A level entered through GameplayMode (the story, `--play-level LEVEL`) brings its scripts' cast (ScriptedCast):
/// player 1 is the scripts' first player, and every other human the scripts create is an AI human (ai::AiHumans on
/// the level's brains) drawn with its own model (play_level_cast.cpp). Otherwise the other characters are a sandbox
/// layout's `target` lines, Coney's passive targets (human::TargetHuman) stepped after the player, and AI fighters (a
/// layout's `fighter` lines, or spawned from the debug menus), humans with a brain stepped in the player's characters'
/// step; both are drawn with the player's model. The parts follow docs/research/characters.md, docs/research/combat.md,
/// docs/research/ai.md and docs/research/camera.md; the mode is Coney's own glue.
///
/// A level entered through gameplay also brings its glass panes and doors (ScriptedCast::objects,
/// play_level_objects.cpp): they get the level's collision mesh and path data, tick twice a step, take player 1's
/// landed hits, and triangle at a pickable door starts a lock pick that takes the pad until it ends.
///
/// It is also the debug menus' way into the game (debug::PlayControls, docs/guides/debug-menu.md): the Player, Camera
/// and Spawner pages act on it between steps, and render() draws the Debug draw page's lines into the scene.
class PlayLevelMode final : public GameMode, public debug::PlayControls, public ScriptedPlayer, public OverlaidLevel {
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

    /// The player in the sandbox `world` (SandboxPlayScenery::create()), at spawn point `spawn` (the layout's first
    /// when unset), with the character loaded from `wad`, as `setup` says (its start is always snapped). Fails as the
    /// scenery and the character loader do.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    createInSandbox(RenderEngine& engine, const io::Wad& wad, sandbox::SandboxWorld world,
                    const std::optional<std::string>& spawn, std::function<void(std::string_view)> print,
                    const PlayerSetup& setup = {});

    ~PlayLevelMode() override;
    PlayLevelMode(const PlayLevelMode&) = delete;
    PlayLevelMode& operator=(const PlayLevelMode&) = delete;
    PlayLevelMode(PlayLevelMode&&) = delete;
    PlayLevelMode& operator=(PlayLevelMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// Preloads the scenery around the camera.
    void enter() override;
    /// One step, as the class comment says. Draws nothing.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// Draws a frame between the last two steps and presents it.
    void render(const RenderTime& time) override;
    /// render() with `overlay` drawn over the HUD before the present: the pause menu over the paused level, the Rumble
    /// intro and result screen over play.
    void renderWithOverlay(const RenderTime& time,
                           const std::function<void(graphics::RenderDevice&)>& overlay) override;
    /// One line: frames, the player's position, heading, speed, gait, clip and state, then the scenery's counts.
    [[nodiscard]] std::string summary() const;

    [[nodiscard]] const human::Player& player() const { return *m_player; }
    /// The sandbox's targets (none in a level).
    [[nodiscard]] std::span<human::TargetHuman* const> targets() const { return m_targetPointers; }
    /// The AI fighters.
    [[nodiscard]] const ai::AiHumans& fighters() const { return *m_ai; }
    [[nodiscard]] const PlayStats& stats() const { return m_stats; }

    /// Writes the trace (human::traceHeader(), then human::traceLine() after every step) to the file at `path`,
    /// replacing it: `--trace`. Fails with ErrorCode::Io when the file cannot be opened.
    [[nodiscard]] std::expected<void, Error> traceTo(const std::string& path);

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

    /// Sets the debug lines render() draws (the debug session's, which must outlive the mode); null draws none.
    void setDebugDraw(const debug::DebugDrawOptions* options) { m_debugDraw = options; }

    // debug::PlayControls, for the debug menus.
    [[nodiscard]] std::string sceneName() const override { return m_scenery->name(); }
    [[nodiscard]] anim::Vec3 playerFeet() const override;
    [[nodiscard]] float playerHeadingDegrees() const override;
    [[nodiscard]] float playerSpeed() const override;
    [[nodiscard]] std::string playerState() const override;
    void teleport(const debug::Place& place) override;
    [[nodiscard]] std::vector<debug::Place> places() const override { return m_scenery->places(); }
    [[nodiscard]] bool playerFrozen() const override { return m_frozen; }
    void setPlayerFrozen(bool frozen) override { m_frozen = frozen; }
    [[nodiscard]] anim::Vec3 cameraEye() const override;
    [[nodiscard]] anim::Vec3 cameraTarget() const override;
    void resetCamera() override { m_player->resetCamera(); }
    [[nodiscard]] bool freeCamera() const override { return m_freeCamera.has_value(); }
    void setFreeCamera(bool on) override;
    [[nodiscard]] bool canSpawn() const override { return m_scenery->canSpawn(); }
    std::expected<void, Error> spawn(const sandbox::Primitive& primitive) override;
    [[nodiscard]] std::size_t spawnedCount() const override { return m_spawned.size(); }
    std::expected<void, Error> clearSpawned() override;
    [[nodiscard]] bool canSpawnFighter() const override { return true; }
    std::expected<void, Error> spawnFighter(anim::Vec3 feet, float headingDegrees) override;
    [[nodiscard]] std::size_t fighterCount() const override { return m_ai->count(); }
    void clearFighters() override;
    [[nodiscard]] bool fightersEngage() const override { return m_ai->engaging(); }
    void setFightersEngage(bool on) override { m_ai->setEngaging(on); }
    [[nodiscard]] std::string fightersState() const override;
    [[nodiscard]] std::vector<debug::CharacterChoice> characterChoices() const override;
    [[nodiscard]] int playerType() const override { return m_type; }
    [[nodiscard]] std::string characterState() const override;
    /// Rebuilds the player as `type` (debug::PlayControls::changeCharacter()): the model a player of the type is drawn
    /// as and its files (the anim set, the moves' Anim Range List, the speeds its clips give), loaded as the mode's
    /// start loads them, and his class as the start's player takes it (human::playerClassOf(): the class's damage
    /// scaled by his Warrior class, his power class), at full health where he stands. The AI
    /// fighters are made again where they stand, at full health, since their brains hold the player they fought. In a
    /// level whose scripts drive the cast the brains and scripts hold the player, so he becomes the type in place
    /// (human::Player::changeCharacter(): the same files and class, at full health) and the scripts' record of him
    /// and his brain take the new type; the cast is left as it is.
    std::expected<void, Error> changeCharacter(int type) override;

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

    /// Skins `character` in `pose`, leaned by `lean` and turned to `heading` at `feet`, into `positions` and `normals`
    /// in the world (RenderWare's axes) for drawing.
    static void skin(const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet, float heading,
                     float lean, std::vector<anim::Vec3>& positions, std::vector<anim::Vec3>& normals);

    /// ScriptedPlayer: a script's `TeleportToFlag` on player 1 during play; no ground snap.
    void teleportPlayer(const world_objects::Placement& placement) override;
    /// ScriptedPlayer: player 1's spray clips for a tag at `point`.
    bool startTagSpray(const std::array<float, 3>& point) override;
    /// ScriptedPlayer: whether player 1's spray loop plays.
    [[nodiscard]] bool tagSprayLooping() const override;
    /// ScriptedPlayer: whether player 1's spray clips still play.
    [[nodiscard]] bool tagSprayPlaying() const override;
    /// ScriptedPlayer: player 1's spray clips end.
    void endTagSpray() override;
    /// ScriptedPlayer: what player 1 can act on for the action prompt: nothing during a mugging, a theft or a lock
    /// pick; else a held human to mug, a pickable door, a freed car stereo and a dealer's offer in reach.
    [[nodiscard]] PromptOffer promptOffer() const override;
    /// The model the player is drawn as.
    [[nodiscard]] const std::string& model() const { return m_model; }

    /// The HUD the mode steps and draws (its own until useHud()), for the HUD page and the scripts.
    [[nodiscard]] hud::Hud* hud() override { return &m_hud->hud(); }
    /// How many HUD sprites the newest step queued (HudLayer::spritesQueued()).
    [[nodiscard]] std::size_t hudSprites() const { return m_hud->spritesQueued(); }
    /// Steps and draws `shared` (the game's HUD, which the scripts' bindings act on; it must outlive the mode) instead
    /// of the mode's own, with the player attached to its panel 0.
    void useHud(hud::Hud& shared);

    /// The level's world objects (the scripts' spawn records brought in round the camera, the objective markers):
    /// what the newest step draws (play_level_world.cpp).
    [[nodiscard]] const world_objects::ObjectTasks& worldObjects() const { return m_objectTasks; }
    /// The glints of the pickups and lock-pickable doors the newest step keeps (docs/research/particles.md#glints).
    [[nodiscard]] const effects::Triglints& glints() const { return m_glints; }
    /// The health rings and L1 markers the newest step queued.
    [[nodiscard]] const hud::HealthRings& healthRings() const { return m_rings; }
    /// The parked cars' parts that came off in the newest step (world_objects::Cars::takeBreaks()), in order: where a
    /// car window's shatter and its glass sound start (docs/research/cars.md#windows).
    [[nodiscard]] const std::vector<world_objects::CarPartBreak>& carBreaks() const { return m_carBreaks; }
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

    // The character and its texture from `wad`, then the mode round `scenery`: what both create functions share.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    createWith(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
               std::function<void(std::string_view)> print, const PlayerSetup& setup = {},
               const ScriptedCast* cast = nullptr);
    // Loads the character `model` names from `wad` and its textures, converted for drawing when `engine` draws: the
    // player's creation, at the start and at a change of character (src/platform/play_level_character.cpp).
    [[nodiscard]] static std::expected<LoadedCharacter, Error> loadCharacter(RenderEngine& engine, const io::Wad& wad,
                                                                             std::string_view model);
    // Draws the player as `loaded` (the model `model`) from now on and keeps its files: a hand-over keeps his
    // animations and class; a change of character in a scripted level rebuilds him as `loaded` first.
    void swapPlayerModel(LoadedCharacter loaded, const std::string& model);
    // The texture a character's dictionaries hold, which every material uses; null when none.
    [[nodiscard]] static rw::Texture* textureOf(const std::vector<TextureDictionary>& dictionaries);
    // The character the player plays: his own after a change of character, else the scene's.
    [[nodiscard]] const human::PlayerCharacter& playerCharacter() const {
        return m_playerCharacter ? *m_playerCharacter : *m_character;
    }

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
    // Makes the scene stage (play_level_scene.cpp).
    void makeStage();
    // One step of the scenes at `nowMs` with pad 1's and 2's buttons (a released human is placed as it is let go,
    // by placeReleased).
    void stepScenes(std::uint64_t nowMs, std::uint16_t buttons);
    // Places a human the scene has let go, where the scene left it.
    void placeReleased(const SceneStage::Release& release);
    // Whether a scene holds player 1 now: his pad does nothing and he is drawn as the scene poses him.
    [[nodiscard]] bool sceneHoldsPlayer() const;
    // The handle the scripts name a cast AI human by (its brain's); 0 for one with no brain.
    [[nodiscard]] double castHandleOf(const ai::AiHuman& fighter);
    // The cast AI human the scripts name `handle`; null when none is.
    [[nodiscard]] const ai::AiHuman* castHumanOf(double handle);
    // Makes the layout's targets, dropped onto the ground, with a mesh each.
    void makeTargets(rw::Texture* texture);
    // Spawns a fighter dropped onto the ground below `spot`, with its mesh.
    void addFighter(anim::Vec3 spot, float headingDegrees);
    // The level's scripts' humans as AI humans: the AI on the scripts' brains, then the scripts' hold released with
    // castHuman() as its spawner, which makes the humans created so far and replays the calls on them.
    void makeCast(const ScriptedCast& cast, const ai::AiConfig& fighters);
    // One human the scripts created, made: player 1's first creation is the player (his brain bound to it); any other
    // with a position an AI human of its type's class, drawn as its model, snapped as `HuCreate` snaps. Returns its
    // brain; null for a creation with no position.
    ai::Brain* castHuman(const HumanCreation& human);
    // HuChangePlayerGang's hand-over to `to` (a cast human): the player is moved, drawn and named as it, and it leaves.
    void takePlace(const ai::Brain& to);
    // The brain type a human of character type `type` fights as: its class's, as castHuman() makes it.
    [[nodiscard]] ai::BrainType castBrainType(int type) const;
    // A character to draw and animate a human as, and its texture.
    struct CastLook {
        const human::PlayerCharacter* character = nullptr;
        rw::Texture* texture = nullptr;
    };
    // The character `model` names, loaded once for every human of it; the scene's character when it is empty or fails
    // to load.
    CastLook castCharacter(const std::string& model);
    // --- play_level_objects.cpp: the level's glass panes and doors.

    // Points `objects` (the scripts' cast's, may be null) at the scenery's collision mesh and path data.
    void bindObjects(world_objects::LevelObjects* objects, const script::RecordedCalls* recorded);
    // The lock pick before the player's step: triangle at a pickable door starts one; while one runs the dial turns,
    // cross judges a pin and triangle or another button abandons. Returns whether it holds the pad this step.
    bool stepLockPick(const Pad& pad);
    // After the player's step: his object attack's hit sent to its object, then the objects' two 60 Hz ticks.
    void stepObjects();
    // Player 1's object attack under way (Player_ObjectAttack's clip, 661 or 662, with its object): whether its hit
    // lands on the object this step. It lands when the clip's strike shapes come on (a car or other object) or strike
    // it (a pane or door, through strikeObjects()), not at the attack's start (docs/research/combat.md#breakables).
    // @orig 0x0021b290 Strike_Contact (unknown)
    [[nodiscard]] std::optional<double> objectAttackLands(const human::Human& human);
    // The strike test's objects (`Strike_Contact`'s object branch, docs/research/combat.md#moving-strikes): `human`'s
    // posed strike `shapes` strike each door or barrier whose enabled triangles they touch, each pane whose body they
    // reach and each strike-target world object (a street prop) whose body (props.h bodyTouches()) they reach, once
    // while the shapes stay on, with message 1 of the attack record's hit kind, each sounding as its type's material;
    // then the level (strikeLevel()). **Coney's stand-ins**: a car's hit is not made here; only the drawn world
    // objects are tested.
    void strikeObjects(human::Human& human, std::span<const human::PosedShape> shapes);
    // The strike shapes' contact with the level mesh: once while the shapes stay on, the fist (or the charging body)
    // against the triangle met, as `human`'s sound (`player`: he is player 1; docs/research/sound-events.md).
    void strikeLevel(human::Human& human, std::span<const human::PosedShape> shapes, bool player);
    // A strike from `attacker` of `kind` on the world object `handle` of `type` at `point`, which stands at `pose` (the
    // prop's `Strike_Contact`, world_objects::Props::strike()), printed as an `objects:` line.
    void strikeProp(double attacker, double handle, const world_objects::ObjectType& type, world_objects::HitKind kind,
                    anim::Vec3 point, anim::Vec3 direction, anim::Vec3 attackerAt, const world_objects::PropPose& pose);
    // The handle `human` goes by as an attacker: player 1's, its brain's, else none.
    [[nodiscard]] double handleOf(const human::Human& human) const;
    // Gives player 1 triangle's pick-up over `pickups` (may be null: none): the search, with sight rays through the
    // level's collision, starts the pick-up on him.
    void bindPickups(LevelPickups* pickups);
    // The car whose freed stereo is in reach of feet at `feet` (a kind-3 context record), nearest first; null for none.
    [[nodiscard]] const world_objects::Car* stereoInReach(anim::Vec3 feet) const;
    // The pickable door nearest feet at `feet` within reach (a kind-2 context record); null for none.
    [[nodiscard]] const world_objects::Door* lockInReach(anim::Vec3 feet) const;
    // Player 1's triangle at a dealer offering a deal (a kind-4 context record, docs/research/ai.md#dealer): the first
    // whose offer is within ai::kDealReach in plan and whose feet are within the prompt height of the human's waist
    // deals. Returns whether one took the press.
    bool tryDeal(human::Human& human);
    // Player 1's triangle beside a Warrior whose swap prompt is up (docs/research/ai.md#warrior-swap): the Warriors
    // ahead within 1.5 m are asked nearest first; the first that does not refuse turns to him over 0.2 s and the two
    // swap what they hold at once, and player 1 says `give_me` when he now holds something. Returns whether one took
    // the press. **Coney's stand-in**: the two-player swap is not built.
    // @orig 0x00306040 WarriorBrain_OnPrompt (unknown)
    bool trySwap(human::Human& human, const world_objects::SightBlocked& blocked);
    // Player 1's flash (command 0x28, d-pad right, docs/research/combat.md#rage): with a flash carried and health
    // below its maximum, a pair he is in is broken, the flash spent and his health filled, and 665 SPECIAL_FLASH plays
    // when nothing blocks a move. **Coney's stand-ins**: the flash is spent on the press, not on the clip's event; its
    // sound is not played; the full-health rage use (upgrade (6, 8)) is not built.
    // @orig 0x002843f8 Player_StartRage (unknown)
    void stepFlash();
    // Player 1's square may strike the world objects that are strike targets, the cars he faces and the level's whole
    // glass panes, aiming at their positions, aim points and centres.
    void giveObjectTargets();
    // The type of the world object `handle` (a spawn record) when its body makes it a strike target; null otherwise.
    [[nodiscard]] const world_objects::ObjectType* worldObjectType(double handle) const;
    // The bodies of the world objects in the world this step (their types' CfgObj shapes at their poses), and the
    // walking humans' slide along the BLOCKHUMANS ones (docs/research/physics.md#bodies). First the RUNTARGET bodies
    // running humans met last step take their strike (Human_OnContact, docs/research/objects.md#trash-props).
    // @orig 0x00391d48 Obj_CreatePhysicsBody (unknown)
    void stepObjectBodies();
    // Player 1's pick-up that reached its clip's event this step: the object is taken.
    void stepPickups();
    // Player 1 lets go of `held`: it leaves the hand from where the hand holds it and falls (dropHeld()), or, `thrown`,
    // flies off with the throw's velocity and spin (docs/research/objects.md#held).
    // @orig 0x00257f38 Human_DropHeld (unknown)
    // @orig 0x002586d8 Human_ReleaseThrow (unknown)
    // @orig 0x003fe6b8 MeleeWeapon_Detach (unknown)
    void dropHeld(human::Human& human, double held, bool thrown = false);
    // The loose objects in flight: their fall, bounce and settle (docs/research/physics.md#settle), their poses written
    // back into their spawn records.
    void stepLooseObjects();
    // Player 1's mugging: the scripts' record for the next one, and the end of one (the money, the mug callback).
    void stepMugging(human::Human& human);
    // The stereo-theft panel follows player 1's theft: shown from the press, its progress each update with cue 0x22 per
    // stage (and 0x23 for the last), gone as the end clip starts (docs/research/hud.md#stereo-layout).
    void stepStereoPanel(const human::Human& human);
    // The mug meter follows player 1's mugging: shown while it runs, with the raw left stick, whether it is on target
    // and both bars' fills (docs/research/hud.md#mug-meter-layout).
    void stepMugMeter(const human::Human& human, const Pad& pad);
    // The lock-pick dial follows player 1's pick: shown at its difficulty while it runs, with the pins' angles and the
    // pin the next press judges (docs/research/hud.md#lock-pick-dial-layout).
    void stepLockPickDial();
    // The level's glass panes, after the opaque world and before the rings (docs/research/objects.md#pane-draw).
    void drawGlass() const;
    // Player 1's handle, as the scripts know him; the nil handle without a cast.
    [[nodiscard]] double playerHandle() const;

    // --- play_level_world.cpp: the world objects and the health rings.

    // Loads the Object List the world objects' models come by (only when the engine draws).
    void makeWorldObjects(const ScriptedCast& cast);
    // The world objects' step round the camera at `eye` (RenderWare's axes), after the scripts and scenes, then the
    // glints of the pickups and lock-pickable doors among them, seen through `view`.
    void stepWorldObjects(world::Vec3 eye, const WorldView& view, std::uint32_t elapsedMs);
    // The glints' step (docs/research/particles.md#glints): a triglint for each `pickup_item` lying in the world and
    // each lock-pickable door, then `ticks` 60 Hz ticks of their blink, a point seen when it is inside `view`'s window
    // within the triglint's distance.
    void stepGlints(const WorldView& view, int ticks);
    // The health rings' step: player 1 and his target, the triggers from `pad`, the camera's heading from `view`.
    void stepRings(const Pad& pad, const WorldView& view, std::uint64_t nowMs);
    // Draws this step's world objects (lit as objects) and the object in player 1's hand at `snapshot`'s pose
    // (docs/research/objects.md#held).
    void drawWorldObjects(const human::PlayerSnapshot& snapshot);
    // Where object `held` hangs in player 1's hand at `snapshot`'s pose; nothing when it is not a record in the hand
    // with a pick-up clip's take event (docs/research/objects.md#held).
    [[nodiscard]] std::optional<world_objects::WorldPose> heldPose(double held,
                                                                   const human::PlayerSnapshot& snapshot) const;
    // Draws this step's health rings and L1 markers, each under where its human is drawn (`feet` by ring id).
    void drawRings(const std::map<std::uint64_t, anim::Vec3>& feet) const;

    // --- play_level_hats.cpp: the hats the humans wear (docs/research/characters.md#hats).

    // One human's hat as the mode keeps it: whether his class's hat was given, the hat worn (0 for none), where it sits
    // on his head and where it was at the last step, and his state then.
    struct Wearer {
        bool dressed = false;
        double hat = 0.0;
        world_objects::HeldAttachment local;
        world_objects::WorldPose pose;
        human::TargetState state = human::TargetState::Standing;
    };
    // After the humans' step: every human's hat put on, replaced or knocked off, and the falling hats moved.
    void stepHats();
    // One human's part of stepHats(), made as `type` (none: no class hat) and posed at this step by `pose` of
    // `character` at `feet` facing `heading`.
    void stepWearer(human::Human& human, std::optional<int> type, const human::PlayerCharacter& character,
                    const anim::Pose& pose, anim::Vec3 feet, float heading);
    // Makes hat `hat` of type `hatName` (when its record is not there yet) and puts it on `human`'s head as `wearer`'s.
    // @orig 0x0024bfc0 Human_SpawnHat (unknown)
    // @orig 0x003a3ba0 Human_PlaceHat (unknown)
    void putOnHat(const human::Human& human, std::optional<int> type, const characters::CharacterType* classRecord,
                  double hat, const std::string& hatName, Wearer& wearer);
    // `wearer`'s hat leaves his head (he faces `heading`) and falls.
    // @orig 0x00258330 Human_KnockOffHat (unknown)
    void knockOffHat(Wearer& wearer, float heading);
    // A deleted human's hat is removed with him.
    void removeWornHat(human::Human& human);
    // Player 1 takes `from`'s place (takePlace()): he wears `from`'s hat, and his own is removed.
    void takeHatOf(const human::Human& from);
    // Notes where `human`'s hat is drawn this frame, `character` posed by `pose` at `feet` facing `heading`.
    void poseHat(const human::Human& human, const human::PlayerCharacter& character, const anim::Pose& pose,
                 anim::Vec3 feet, float heading);
    // Places this frame's worn hats among the world objects (before PlacedObjects::draw()).
    void drawHats();

    // Draws the humans: their lights, the render states, each mesh in its two passes.
    void drawCharacter() const;
    // The blood texture of the second pass of a human with `health`, or null when it shows none.
    [[nodiscard]] rw::Texture* bloodTextureFor(const combat::Health& health) const;
    // The view from a camera pose (RenderWare's axes) through the player camera's lens, with `drawDistance`.
    [[nodiscard]] WorldView viewFrom(const world::CameraPose& pose, float drawDistance,
                                     const camera::CameraLens& lens = camera::kPlayerCameraLens) const;
    // Gives each human in the step the clip replacements its script state names (`HuUseAnim`, `HuUseAnyAnim`), when
    // they changed: the idle's through Human::setIdleClip(), the others' through Human::setOverrideClip().
    void applyAnimOverrides();
    // The free camera `camera` between its last two steps, `alpha` of the way.
    [[nodiscard]] static world::DebugCamera blendedFreeCamera(const Interpolated<world::DebugCamera>& camera,
                                                              float alpha);
    // Draws the Debug draw page's lines through the current camera, from `snapshot`.
    void drawDebugLines(const human::PlayerSnapshot& snapshot) const;

    RenderEngine& m_engine;
    const io::Wad& m_wad;
    // The scripts' dynamic clips (HuUseAnim's idle replacements), loaded from the disc when first named.
    characters::DynamicClips m_dynamicClips{
        [this](std::string_view name) { return characters::loadAnimResource(m_wad, name); }};
    std::unique_ptr<PlayScenery> m_scenery;
    std::unique_ptr<human::PlayerCharacter> m_character; // the scene's: the targets and fighters play it
    std::vector<TextureDictionary> m_dictionaries;       // before the mesh, which holds a reference to their texture
    // The humans' shared blood textures, before every mesh, whose dual layers hold references to them.
    std::optional<BloodTextures> m_blood;
    // After a change of character, the player's own character and textures; and the characters he played before,
    // kept while the mode lasts because a target may still be playing a paired clip from one of their anim sets.
    std::unique_ptr<human::PlayerCharacter> m_playerCharacter;
    std::vector<TextureDictionary> m_playerDictionaries;
    std::vector<std::unique_ptr<human::PlayerCharacter>> m_retired;
    characters::CharacterTypes m_types; // the configuration's types
    int m_type = 0;                     // the type the player is
    int m_levelNumber = 0;              // the scene's level number (its models), 0 for a sandbox
    std::unique_ptr<human::Player> m_player;
    std::function<void(std::string_view)> m_print;
    std::vector<anim::Vec3> m_positions;
    std::vector<anim::Vec3> m_normals;
    std::unique_ptr<CharacterMesh> m_mesh;
    std::unique_ptr<PlayLighting> m_lights; // the level's lights (the cast's), or a stand-in
    Interpolated<float> m_drawDistance;     // at the last two steps
    PlayStats m_stats;
    std::uint32_t m_lastAnimId = 0;
    const debug::DebugDrawOptions* m_debugDraw = nullptr;
    bool m_frozen = false;
    std::optional<CameraPin> m_pinnedCamera;                      // `--camera`
    bool m_freezeWorld = false;                                   // `--freeze-world`
    std::optional<std::uint64_t> m_frozenSince;                   // the game ticks of the step the world froze after
    std::optional<Interpolated<world::DebugCamera>> m_freeCamera; // at the last two steps, while it is on
    std::vector<sandbox::Primitive> m_spawned;
    // The sandbox's targets, each drawn with its own copy of the character's mesh.
    struct Target {
        std::unique_ptr<human::TargetHuman> human;
        std::unique_ptr<CharacterMesh> mesh;
        std::vector<anim::Vec3> positions;
        std::vector<anim::Vec3> normals;
    };
    std::vector<Target> m_targets;
    std::vector<human::TargetHuman*> m_targetPointers;
    std::vector<human::Combatant*> m_combatants; // the targets, as the player's step takes them
    // The characters the scripts' humans are drawn as, by model, before the AI so they outlive the humans playing them.
    std::map<std::string, LoadedCharacter, std::less<>> m_castCharacters;
    ScriptedCast m_cast;            // the level's scripts' humans and brains; all null without them
    bool m_castPlayerBound = false; // whether player 1's creation is bound to the player
    // The level's route planner (null without path data), before the AI so it outlives the brains that use it.
    std::unique_ptr<ai::RoutePlanner> m_planner;
    // The AI fighters, and a mesh each (in the fighters' order).
    std::unique_ptr<ai::AiHumans> m_ai;
    struct FighterMesh {
        const human::PlayerCharacter* character = nullptr; // the character it is skinned as
        std::unique_ptr<CharacterMesh> mesh;
        std::vector<anim::Vec3> positions;
        std::vector<anim::Vec3> normals;
        bool hidden = false; // its human was deleted
    };
    std::vector<FighterMesh> m_fighterMeshes;
    rw::Texture* m_texture = nullptr; // the character's texture, for new meshes
    std::string m_model;              // the Character List model the player is
    // The scenes: the stage drawn and stepped with them, the system attached (or the test aid's own) and player 1's
    // handle in them.
    std::unique_ptr<SceneStage> m_stage;
    scenes::SceneSystem* m_scenes = nullptr;
    std::unique_ptr<scenes::SceneList> m_ownSceneList;
    std::unique_ptr<scenes::SceneSystem> m_ownScenes;
    double m_playerHandle = 0.0;
    // The level's glass panes and doors (gameplay's; null without them), the lock pick under way, and its difficulty.
    world_objects::LevelObjects* m_objects = nullptr;
    double m_heldObject = 0.0;             // what player 1 held at the last step (world_objects::kNoObject for nothing)
    world_objects::Cars* m_cars = nullptr; // the level's parked cars, for their stereos; not owned
    std::optional<double> m_theftCar;      // the car whose stereo player 1 is stealing
    // Player 1's object attack waiting for its strike window (objectAttackLands()).
    struct ObjectAttack {
        double object = 0;      // the object it aims at
        std::uint32_t clip = 0; // its clip, 661 or 662
        bool shapesOn = false;  // its strike shapes have come on
    };
    std::optional<ObjectAttack> m_objectAttack;
    std::vector<world_objects::CarPartBreak> m_carBreaks; // car parts off in the newest step (carBreaks())
    bool m_wasMugging = false;                            // player 1 was mugging at the last step
    std::optional<bool> m_mugEnding;                      // a decided mugging's result, until its end clip finishes
    std::uint32_t m_mugEndClip = 0;                       // that end clip
    LevelPickups* m_pickups = nullptr;                    // the level's loose objects for the pick-up; not owned
    int m_theftStage = 0;                                 // the theft's stage the panel last showed
    std::optional<world_objects::LockPick> m_lockPick;
    int m_lockPickDifficulty = 0;
    bool m_flashRingRequest = false;      // a flash used: the HUD's ring request (docs/research/hud.md)
    script::SoundHost* m_sound = nullptr; // the game's sound for the dealers' lines; not owned
    // The --trace file (closed when unset) and the steps traced.
    std::optional<std::ofstream> m_trace;
    std::uint64_t m_traceSteps = 0;
    // The level's parked cars, particles and motion blur (play_level_effects.h), when gameplay brought them.
    std::unique_ptr<PlayLevelEffects> m_levelEffects;
    // The world objects: the scripts' records and types (null without a cast), their objects, the models they are
    // drawn with (null without pixels or an Object List), and the health rings.
    world_objects::SpawnRecords* m_records = nullptr;
    const world_objects::ObjectTypes* m_objectTypes = nullptr;
    world_objects::ObjectTasks m_objectTasks;
    effects::Triglints m_glints;
    world_objects::ObjectBodies m_objectBodies; // this step's world-object bodies
    // The RUNTARGET bodies running humans met last step: (the human's handle, his feet, the object), struck next step.
    std::vector<std::tuple<double, anim::Vec3, double>> m_runContacts;
    world_objects::LooseObjects m_looseObjects;              // the dropped objects falling or settling
    combat::CombatRandom m_throwRandom{0x7417U};             // a thrown object's spin (a fixed seed)
    std::unique_ptr<world_objects::ObjectList> m_objectList; // before the models that read it
    std::unique_ptr<PlacedObjects> m_placed;
    // The hats: the fittings (read once from the recorded `CfgHat` calls), each human's, the handles worn, those
    // falling, and where this frame draws the worn ones.
    world_objects::HatFits m_hatFits;
    bool m_hatFitsRead = false;
    std::map<const human::Human*, Wearer> m_wearers;
    std::set<double> m_wornHats;
    world_objects::FallingHats m_fallingHats;
    std::vector<std::pair<double, world_objects::WorldPose>> m_hatDraws;
    hud::HealthRings m_rings;
    std::map<std::uint64_t, int> m_ringHealth; // each ringed human's health at the last step, for its hit pulse
    std::unique_ptr<HudLayer> m_hud;           // the HUD's sheets, batches and pass (src/platform/hud_layer.h)
    // The layer renderWithOverlay() adds over the HUD for the render it runs; null otherwise.
    const std::function<void(graphics::RenderDevice&)>* m_overlay = nullptr;
};

} // namespace coney::platform
