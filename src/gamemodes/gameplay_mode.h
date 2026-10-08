// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ai/brains.h"
#include "ai/crew.h"
#include "ai/scripted_brains.h"
#include "camera/cameras.h"
#include "core/error.h"
#include "core/pads.h"
#include "effects/level_effects.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/level_object_services.h"
#include "gamemodes/level_pickups.h"
#include "gamemodes/level_start.h"
#include "gamemodes/loading_screen.h"
#include "gamemodes/movie_player.h"
#include "gamemodes/play_overlay.h"
#include "graphics/level_lighting.h"
#include "graphics/render_device.h"
#include "scenes/scene_player.h"
#include "scripting/message_handlers.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "warriors/tag_session.h"
#include "world_objects/cars.h"
#include "world_objects/flag_net.h"
#include "world_objects/flags.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_types.h"
#include "world_objects/radios.h"
#include "world_objects/spawn_records.h"
#include "world_objects/tag_spots.h"
#include "world_objects/trigger_spheres.h"
#include "world_objects/volume_boxes.h"

namespace coney {

/// A loaded level whose player 1 a script can move while it plays: a `TeleportToFlag` on player 1 after the level
/// loaded (the hub's door walk starts 100 ms into play) reaches the level through this. The play mode implements it.
/// What player 1 can act on this frame that only the level knows, for the action prompt
/// (docs/research/crimes.md#triangle): each field a `GSTRING.HUD` id, or none.
struct PromptOffer {
    /// A mini-game, a mugging or a theft holds him: no prompt at all.
    bool blocked = false;
    /// He holds a human he can mug: 1 (mug), or 0 (interrogate).
    std::optional<std::uint32_t> held;
    std::optional<std::uint32_t> lock;   ///< A pickable door in reach (kind 2): 15.
    std::optional<std::uint32_t> stereo; ///< A freed car stereo in reach (kind 3): 16.
    std::optional<std::uint32_t> dealer; ///< A dealer's offer in reach (kind 4): his type's prompt.
};

class ScriptedPlayer {
  public:
    virtual ~ScriptedPlayer() = default;
    ScriptedPlayer() = default;
    ScriptedPlayer(const ScriptedPlayer&) = delete;
    ScriptedPlayer& operator=(const ScriptedPlayer&) = delete;
    ScriptedPlayer(ScriptedPlayer&&) = delete;
    ScriptedPlayer& operator=(ScriptedPlayer&&) = delete;

    /// Puts player 1 at `placement` (feet, heading in degrees) with no ground snap, as `TeleportToFlag` does.
    virtual void teleportPlayer(const world_objects::Placement& placement) = 0;
    /// Player 1 starts a tag's spray clips facing the tag at `point` (Human::startTagSpray()); false when he cannot.
    virtual bool startTagSpray(const std::array<float, 3>& /*point*/) { return false; }
    /// Whether player 1's spray intro has ended and its loop plays: the stick game goes live.
    [[nodiscard]] virtual bool tagSprayLooping() const { return true; }
    /// Whether player 1's spray clips still play (false: something else took the body).
    [[nodiscard]] virtual bool tagSprayPlaying() const { return true; }
    /// Player 1's spray is over: his spray clips end (Human::endTagSpray()).
    virtual void endTagSpray() {}
    /// What player 1 can act on this frame besides the scripts' action objects (kind 1), for the action prompt.
    [[nodiscard]] virtual PromptOffer promptOffer() const { return {}; }
};

/// What a level's scripts drive, which gameplay gives the level it loads: the humans the scripts create, the brains
/// and gangs they give goals to, and player 1's cameras. The scripts' hold on the brains holds their calls until the
/// level makes the humans (ai::ScriptedBrains::release()). Everything is gameplay's, valid until the level is unloaded.
///
/// Research: docs/research/ai.md#coney, docs/research/characters.md#creation
struct ScriptedCast {
    CreatedHumans* humans = nullptr;                 ///< The scripts' humans, in the order of their `HuCreate` calls.
    const script::RecordedCalls* recorded = nullptr; ///< The configuration the scripts recorded (the classes).
    ai::Brains* brains = nullptr;                    ///< The level's brains, gangs and formations.
    ai::ScriptedBrains* scripted = nullptr;          ///< The scripts' hold on them.
    camera::Cameras* cameras = nullptr;              ///< Player 1's cameras, which the level hands its player.
    scenes::SceneSystem* scenes = nullptr;           ///< The level's scenes, for the level to host; null for none.
    /// The level's glass panes and doors the scripts spawned; the level gives them its collision mesh and path data
    /// (LevelObjects::world), steps them and sends them its hits.
    world_objects::LevelObjects* objects = nullptr;
    graphics::LevelLighting* lighting = nullptr; ///< The level's lights and fog, as its scripts set them.
    effects::LevelEffects* effects = nullptr;    ///< The level's particles and motion blur, which it draws.
    world_objects::Cars* cars = nullptr;         ///< The level's parked cars, which it draws and stands.
    LevelPickups* pickups = nullptr;             ///< The level's loose objects for triangle's pick-up; null for none.
    /// The level's spawn records and object types: the world objects the level draws (world_objects::ObjectTasks).
    world_objects::SpawnRecords* records = nullptr;
    const world_objects::ObjectTypes* types = nullptr;
    /// `HuForceEnableReticule`'s flag (GameState::forceReticules): every player's health rings at full alpha.
    const bool* forceReticules = nullptr;
    /// The game's sound, for the humans' speech commands the level says itself (a dealer's lines); null for none.
    script::SoundHost* sound = nullptr;
    /// The world objects' handle counter (script::ScriptSystem::nextObjectHandle()), for the objects the level makes
    /// itself (a human's hat); empty for none.
    std::function<double()> objectHandles;
};

/// A loaded level that can draw a 2D layer over its frame just before the frame is presented: the pause menu over the
/// paused world (docs/research/pause.md). The play mode implements it.
class OverlaidLevel {
  public:
    virtual ~OverlaidLevel() = default;
    OverlaidLevel() = default;
    OverlaidLevel(const OverlaidLevel&) = delete;
    OverlaidLevel& operator=(const OverlaidLevel&) = delete;
    OverlaidLevel(OverlaidLevel&&) = delete;
    OverlaidLevel& operator=(OverlaidLevel&&) = delete;

    /// Draws the level's frame for `time` as render() does, with `overlay` drawn over everything else (the HUD
    /// included) before the present.
    virtual void renderWithOverlay(const RenderTime& time,
                                   const std::function<void(graphics::RenderDevice&)>& overlay) = 0;
};

class PauseMode;

/// Game mode 1, gameplay: one loaded level. The level flow (mode 8) selects a level and pushes it; its enter runs
/// `InitLevel`, and each update is a frame of play.
///
/// `InitLevel` here is its script step and the level's loading, in the original's order where it matters: the level
/// script runs first (runLevelScript(): `global.lua`, then `<level>.lua`, whose `HuCreate` makes player 1 at the
/// checkpoint's start), then the level loads with the player at that start and preloads around him. The loading,
/// drawing and the player are the platform's: a LevelLoader makes them as a mode of their own, which this mode runs
/// in its place (Coney's play mode, `src/platform/play_level_mode.h`).
///
/// Coney's stand-ins (docs/research/level-loading.md#coneys-implementation):
/// - The sound (`context`'s sound host) is told of mode 1's enter, the load screen's start (InitLevel step 3), its end
///   (step 11) and the exit. The rest of `InitLevel` (the object list, the dependency list) and of mode 1's enter (the
///   level-end countdown) is not there yet: the player has control on the first frame. The start callback runs before
///   the level loads (runLevelScript()). The intro movie (`L99_IN`) goes to the movie player after the level loaded
///   (setMoviePlayer(): the movie player pushes itself over gameplay, which waits beneath it until it ends).
/// - With a loading screen (setLoadingScreen(), the story flow), enter only begins it; the updates then fade it in
///   over 200 ms of game time, load the level in one step (the window keeps the faded-in picture meanwhile, as the
///   original keeps its last frame between reads), hold it until kLoadScreenHoldMilliseconds after its start (Coney's
///   loads take no game time, so this stands for the PS2's load), finish it (about 170 ms of fade out), and only then
///   ask for the intro movie and run the level's first step. The pads are not read meanwhile. Without one (`coney
///   --play-level`, most tests), the level loads in enter and plays from the first update.
/// - The level's brains and gangs are made before its script (the AI host of `context`), but the humans the script
///   creates are made, and the calls on them run, only once the level has loaded its characters (ScriptedCast).
/// - So are player 1's cameras (`context`'s cameras), which the script's camera calls set up before the player exists
///   (camera::Cameras holds them until his follow camera is attached); the level hands them to its player. They find
///   the human `CamSetSecondary` names through the scripts' hold on the brains (its live position).
/// - Each frame after the level's step: the animation callbacks of the anims the scripts' humans started, then the
///   volume boxes' trigger update over those humans (their messages to the objects' handlers in `context`), then the
///   trigger spheres' (each checked every fifth frame; a sphere's object found among the humans, flags and spawn
///   records, its clear-line test against the level's collision mesh), then the
///   players' frame (runPlayerFrame(): the Lua pad handlers, the stopwatch, the wanted timers), then the scripts'
///   frame (scheduled calls).
/// - The level's scenes (setSceneMaker()) are made before its script, as `context`'s scene system, and handed to the
///   level in its ScriptedCast, which hosts and steps them; their callbacks call the scripts. Without a maker the
///   scene bindings run Coney's stand-in (docs/research/scenes.md#coneys-implementation).
/// - A teleport of player 1 by a script during play (HumanCreation::teleports changes) is handed to the level when it
///   is a ScriptedPlayer.
/// - The level's glass panes and doors (world_objects::LevelObjects, docs/research/objects.md) are gameplay's: the
///   bindings in `context` spawn them while the level script runs, with the glass types the boot scripts recorded
///   applied first; the level gives them its collision mesh and path data and steps them (ScriptedCast::objects).
///   Their script callbacks, the `CrimeScene` flag, their sounds, their crime reports and statistics (to the players'
///   state, GameState::player) go through LevelObjectServices.
/// - A level that fails to load leaves the frame black, with the error logged.
/// - The level's effects (effects::LevelEffects: the particle systems `SpawnParticle` and the engine start, and the
///   motion blur) are made before its script and stepped after the scripts' frame; the level draws them. So are its
///   parked cars (world_objects::Cars, `CarSpawn`), which the level draws and makes obstacles of.
/// - Leaving (exit) is `UnloadLevel`'s script part only: a fresh Lua state.
///
/// Research: docs/research/level-loading.md#mode-1, docs/research/level-loading.md#story-into-level99
class GameplayMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 1;

    /// Loads the level `start` names, with player 1 at `start` (or the platform's stand-in start when the script made
    /// none), as a mode this one runs, the scripts' humans made from `cast`. Fails as the level's loaders do.
    using LevelLoader = std::function<std::expected<std::unique_ptr<GameMode>, Error>(const LevelStart& start,
                                                                                      const ScriptedCast& cast)>;

    /// Draws through `device` while no level is loaded, runs the level scripts in `scripts` (whose bindings work on
    /// `context`, where each level's brains are set as the AI host), reads the checkpoint and the start callback in
    /// `state`, keeps the scripts' humans in `humans` and flags in `flags`, and loads levels with `loader`; each must
    /// outlive the mode. `recorded` is the configuration the scripts record. `log` gets a line for the start and for a
    /// level that fails.
    GameplayMode(graphics::RenderDevice& device, script::ScriptSystem& scripts, script::BindingContext& context,
                 GameState& state, CreatedHumans& humans, world_objects::WorldFlags& flags,
                 const script::RecordedCalls& recorded, LevelLoader loader, std::function<void(std::string_view)> log);
    ~GameplayMode() override;
    GameplayMode(const GameplayMode&) = delete;
    GameplayMode& operator=(const GameplayMode&) = delete;
    GameplayMode(GameplayMode&&) = delete;
    GameplayMode& operator=(GameplayMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets the level the next enter loads (the level flow's selection).
    void setLevel(std::string level) { m_levelName = std::move(level); }
    /// Plays the level's intro movie through `player` (null: none); it must outlive its use here.
    void setMoviePlayer(MoviePlayer* player) { m_moviePlayer = player; }
    /// Makes each level's scene system (over the disc's scene list); null or empty: no scenes.
    using SceneMaker = std::function<std::unique_ptr<scenes::SceneSystem>()>;
    void setSceneMaker(SceneMaker maker) { m_sceneMaker = std::move(maker); }
    /// Plays the glass panes' and doors' sounds through `sounds` (the audio's ObjectSounds; null: none), which must
    /// outlive the mode or be replaced first.
    void setObjectSounds(world_objects::ObjectServices* sounds) { m_objectServices.setSounds(sounds); }
    /// `--freeze-world`: once the level has played one step, a step runs only the level's own (frozen) step: no
    /// scripts, effects, spawners, radios or other world updates, so nothing in the world advances.
    void setWorldFrozen(bool frozen) { m_worldFrozen = frozen; }

    /// Shows `screen` while each level loads (null: none, the level loads in enter); it must outlive the mode.
    void setLoadingScreen(LoadingScreen* screen) { m_loadingScreen = screen; }

    /// Coney's stand-in for how long a level's load lasts on the PS2, from the loading screen's start to its finish, in
    /// game time: 3,000 ms (level99 took "a few seconds" at runtime, docs/research/level-loading.md#loading-screen).
    static constexpr std::uint64_t kLoadScreenHoldMilliseconds = 3000;

    /// Where a level start is: behind the loading screen, or playing.
    enum class Phase : std::uint8_t {
        FadeIn,  ///< The screen fades in; the level is not loaded yet.
        Loading, ///< The level is loaded; the screen holds for the stand-in load time.
        FadeOut, ///< The screen's finish.
        Playing, ///< Frames of play.
    };
    /// The current phase (Playing without a loading screen).
    [[nodiscard]] Phase phase() const { return m_phase; }

    /// `InitLevel`: the level's brains, the level script, then the level from the loader, entered (its preload).
    /// @orig 0x001582e0 Mode1::Enter (unknown)
    /// @orig 0x0015fe90 InitLevel (InitLevel.cpp)
    void enter() override;

    /// A frame of play: the loaded level's step, then the scripts' frame, then a teleport of player 1 the scripts made
    /// handed to the level. Leaves when the level does.
    /// @orig 0x00158728 Mode1::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// The level's frame, or black while none is loaded.
    void render(const RenderTime& time) override;

    /// The level's frame with `overlay` drawn over it before the present (the paused world under the pause menu):
    /// through the level when it is an OverlaidLevel; otherwise the level's frame without the overlay, or black with
    /// it while no level is loaded.
    void renderWithOverlay(const RenderTime& time, const std::function<void(graphics::RenderDevice&)>& overlay);

    /// Lets START pause the game through `pause` (not owned; null: no pause): each frame of play ends with
    /// PauseMode::playFrame().
    void setPause(PauseMode* pause) { m_pause = pause; }

    /// Steps `overlay` (not owned; it must outlive the mode) after the scripts' frame of each frame of play, and draws
    /// it over the level's frame while it is showing (the Rumble intro).
    void addOverlay(PlayOverlay* overlay) { m_overlays.push_back(overlay); }

    /// A frame of play without START's pause check: what update() runs before it. The Rumble result (mode 0x14), over
    /// gameplay, keeps the world going with it.
    ModeResult updateWorld(GameModeStack& stack, const FrameTime& frame);

    /// Ends the level, its brains and gangs, and makes a fresh Lua state (`UnloadLevel`'s script part).
    void exit() override;

    /// Hides the HUD under the mode pushed over play, then passes the suspend on to the loaded level.
    /// @orig 0x00158660 GameMode1_Suspend (unknown)
    void suspend() override;
    /// Shows the HUD again (RestoreHud) unless player 1 is in a scene, then passes the resume on to the loaded level.
    /// @orig 0x00158580 GameMode1_Resume (unknown)
    void resume() override;

    /// Whether a level loader was given: without one, every level fails to load.
    [[nodiscard]] bool loads() const { return static_cast<bool>(m_loader); }
    /// The loaded level's mode; null before enter or when the level failed to load.
    [[nodiscard]] GameMode* level() const { return m_level.get(); }
    /// Where the level script started player 1 (set by enter).
    [[nodiscard]] const std::optional<LevelStart>& start() const { return m_start; }
    /// The level's brains and gangs; null while no level is entered.
    [[nodiscard]] const ai::Brains* brains() const { return m_brains.get(); }
    /// Player 1's cameras for the level; null while no level is entered.
    [[nodiscard]] camera::Cameras* cameras() const { return m_cameras.get(); }
    /// The level's glass panes and doors (empty while no level is entered).
    [[nodiscard]] world_objects::LevelObjects& objects() { return m_objects; }
    /// The level's particles and motion blur; null while no level is entered.
    [[nodiscard]] effects::LevelEffects* effects() const { return m_effects.get(); }
    /// Player 1's spray under way (HuTag); null when he is not tagging.
    [[nodiscard]] const TagSession* tagSession() const { return m_tagSession ? &*m_tagSession : nullptr; }
    /// The level's tag spots.
    [[nodiscard]] const world_objects::TagSpots& tagSpots() const { return m_tagSpots; }
    /// The loose objects and action objects a player uses with triangle; null while no level is entered.
    [[nodiscard]] const LevelPickups* pickups() const { return m_pickups ? &*m_pickups : nullptr; }

  private:
    // InitLevel's script step and the level from the loader, entered (its preload).
    void loadLevel();
    // The end of InitLevel: the intro movie; play follows.
    void startPlay();
    // The loading screen's phases for one step; true once play may run in this step.
    bool updateLoadingScreen(const FrameTime& frame);
    // Drops the level, then player 1's cameras, then the scripts' hold on its brains (no longer the AI host), then the
    // brains.
    void endLevel();
    // The volume boxes' trigger update over the scripts' humans, their messages going to the objects' handlers
    // (docs/research/scripting.md#triggers). In the original the boxes update with the other tasks in the world step.
    void updateBoxes(std::uint64_t nowMs);
    // Message 6 to every volume box `human` stands in: he damaged `object` (docs/research/scripting.md#triggers).
    void sendDamageMessage(double human, double object);
    // The radios' update (Radio_Update): their sounds through the game's sound, the player's place and the progress.
    void updateRadios();
    // The particle systems' light types in the level's light manager: one point light, in its colour this step, for
    // each alarm strobe's `strober` and neon sign's light, none for one ended or gone
    // (docs/research/script-types.md#light-type-lights).
    void syncSystemLights();
    // The particle systems' script emitters: an alarm strobe's alarm loop, on while it is
    // (docs/research/script-types.md#strober), and a garbage pile's flies' loop (#flies).
    void syncSystemSounds();
    // Hands the sounds the scripts' humans asked for this step (their clips' animation sounds, the hits they took) to
    // the game's sound, with what it reads of each (docs/research/sound-events.md).
    void reportHumanSounds();
    // HuTag (docs/research/crimes.md#tagging): player 1 with paint starts the stick game at the spot, without paint
    // says 37 `nopaint` and gets event 14 unfinished; another human is the spot's tagger at once (Coney's stand-in
    // for the walk to the flag). A spray that starts calls the start callback.
    void startTag(double human, double tag, double flag);
    // The spray's start reaching the scripts (docs/research/crimes.md#tag-callbacks): `CfgTagStartCallback`'s
    // function with the tagger, the tag and the flag, no result asked; nothing when no name is set.
    // @orig 0x00238f50 Tag_CallStartCallback (unknown)
    void callTagStart(double human, double tag, double flag);
    // The tag spots' update every second 60 Hz tick, and player 1's stick game on pad 1's left stick: a snap-back
    // has a crew mate say 80 `tagcheer` when the tagger's tag cheer is on; its end frees the pad, has a crew mate say
    // 83 `tagdone` on a finish and sends him event 14 (Tag_End).
    void updateTagging(const Pads& pads, double seconds);
    // A crew mate of `tagger` says `command` (docs/research/sound-events.md#tag-lines): only for a war chief whose
    // last Warrior command is defend; one member of his gang within his far melee range is picked, and he speaks
    // only when he may comment on tags.
    // @orig 0x00273a68 Tag_SayNearbyLine (unknown)
    void sayTagLine(double tagger, std::uint32_t command);
    // Player 1's tagging panel follows his stick game: the path, the painted cells, the cursor, the charge left and
    // the paint's colour each update; hidden with no game (docs/research/hud.md#fn-after-subtitle).
    void updateTagPanel();
    // The spray's start once its intro clip has played (0x0022e610): the stick game, message 0 to the tag, the start
    // callback.
    void beginTagSpray();
    // Player 1's Warrior command menu on R2 and the right stick at game time `nowMs`, before the level's step so the
    // camera's stick is off on the frame it opens (gameplay_war_commands.cpp).
    void updateWarCommandMenu(const Pads& pads, std::uint64_t nowMs);
    // The combat tutorial's callback (`HUDSetTutorialCallback`) with the anim id of each hit player 1 struck in the
    // level's step, landed or blocked (docs/research/hud.md#tutorial-callback). The original calls it from the damage
    // step itself; Coney calls it right after the step.
    // @orig 0x001ce9a8 Tutorial_CallCallback (unknown)
    void callTutorialCallback();
    // The `PadSetHandlerEx` handler, once per human with a pad command this step (its handle, the command, 1),
    // whether or not the pad is locked or the command disabled (docs/references/bindings/input.md#padsethandlerex).
    // **Coney choices**: called after the level's step, not before each human acts; only the pad-driven humans
    // carry a pad command, as Coney's brains write their commands straight to the record.
    // @orig 0x001480e0 Pad_CallLuaHandlerEx (unknown)
    void callPadHandler();
    // Whether player 1 holds a role in a starting or playing scene (Human_IsInSceneState, as far as Coney tracks it).
    // @orig 0x00227d28 Human_IsInSceneState (unknown)
    [[nodiscard]] bool playerInScene() const;
    // Player 1's crew this frame: the follow the game issues itself (forced) when he is made a player and when a scene
    // that held him ends, once no scene camera is current; then his automatic commands (ai::CrewOrders), and his
    // menu unlocked every 10 updates (docs/research/ai.md#warrior-follow, #warrior-auto-commands).
    // @orig 0x00229c40 Human_MakePlayer (unknown)
    // @orig 0x0039f450 SceneTask_End (unknown)
    // @orig 0x003035d8 PlayerBrain_Update (unknown)
    void stepCrew(std::uint64_t nowMs);
    // Gives the hub's host (ai::ScriptedHub) what it reads beyond the brains: the configuration's categories and flee
    // percentages, the workout's tuning, the volume boxes and the flags inside them, the crimes and the crime scene.
    void wireHub();
    // Where a trigger sphere's object is: a human the scripts made, a flag or a spawn record; nothing when gone.
    [[nodiscard]] std::optional<std::array<float, 3>> objectPosition(double handle) const;
    // Where a prompt object with no spawn record is: a particle system (a tag spot) or a flag; nothing otherwise.
    [[nodiscard]] std::optional<anim::Vec3> promptObjectPosition(double handle) const;
    // Player 1's feet as the scripts see them; nothing with no player 1.
    [[nodiscard]] std::optional<anim::Vec3> playerFeet() const;
    // HUD_Update's choice of player 1's action prompt (docs/research/hud.md#action-prompts), as far as Coney has it:
    // a held human to mug, a downed partner to revive (revivePrompt()), a cuffed human to free (kind 0,
    // uncuffPrompt()), the action object's text (with its hint), a pickable door, a car stereo, a dealer's offer (the
    // level's promptOffer()); none while he sprays, frees a cuffed human, is in a scene or a mini-game holds him.
    // **Coney's stand-in**: a talkable human is not chosen yet.
    // @orig 0x001af010 HUD_Update (unknown)
    void updateActionPrompt();
    // The uncuffing (gameplay_uncuff.cpp, docs/research/crimes.md#uncuffing). An arrest or a release (the
    // ScriptedHumans hook): a friendly AI human arrested says 25 `arrested`.
    void onArrest(ai::Brain& brain, bool arrested);
    // The friendly AI human in cuffs nearest player 1 within the kind-0 reach (`CfgActionDistance` 0) and 1.5 m of his
    // waist, not being freed already; null when none.
    [[nodiscard]] ai::Brain* cuffedInReach() const;
    // The kind-0 prompt, `GSTRING.HUD` 2, while a cuffed human is in reach and no mash runs; empty otherwise.
    [[nodiscard]] std::string uncuffPrompt() const;
    // The knocked-out partner player 1 could revive (`Human_FindRevivableNear`): the nearest revivable human friendly
    // to him within 3 m, not cuffed, in sight; null when none (docs/research/hud.md#action-prompts).
    // @orig 0x00279078 Human_FindRevivableNear (unknown)
    [[nodiscard]] ai::Brain* revivableInReach() const;
    // The revive prompt, `GSTRING.HUD` 4, while a partner is revivable and player 1 holds a flash; empty otherwise.
    [[nodiscard]] std::string revivePrompt() const;
    // Triangle by a cuffed human (`ContextAction_Use` kind 0): `freer` starts the mash; false when none is in reach.
    bool startUncuff(human::Human& freer);
    // Each frame after the level's step: installs the triangle hook on player 1, and ends a mash that has an outcome,
    // was cut short or lost its cuffed human (`MiniGame_Update` mode 1).
    // @orig 0x00255f08 MiniGame_Update (unknown)
    // @orig 0x00260a70 Uncuff_MashFail (unknown)
    void updateUncuff();
    // A mash that filled: the cuffed human is released and both play their ends.
    void uncuffSucceeded(human::Human& freer, ai::Brain& cuffed);

    bool m_worldFrozen = false;    // setWorldFrozen()
    bool m_frozenStepDone = false; // the step before the freeze has run
    graphics::RenderDevice& m_device;
    script::ScriptSystem& m_scripts;
    script::BindingContext& m_context;
    GameState& m_state;
    CreatedHumans& m_humans;
    world_objects::WorldFlags& m_flags;
    const script::RecordedCalls& m_recorded;
    LevelLoader m_loader;
    MoviePlayer* m_moviePlayer = nullptr; // the intro movie's player; not owned
    SceneMaker m_sceneMaker;
    LoadingScreen* m_loadingScreen = nullptr; // shown while a level loads; not owned
    Phase m_phase = Phase::Playing;
    std::optional<std::uint64_t> m_screenStartMs; // the screen's start: set by the first update after enter
    std::function<void(std::string_view)> m_log;
    std::string m_levelName;
    std::optional<LevelStart> m_start;
    // The level's brains and the scripts' hold on them, before the level, whose humans they refer to.
    std::unique_ptr<ai::Brains> m_brains;
    std::unique_ptr<ai::ScriptedBrains> m_scripted;
    ai::CrewOrders m_crewOrders;  // player 1's automatic commands
    double m_crewChief = 0.0;     // player 1's handle when his crew was last given the follow
    bool m_crewInScene = false;   // whether a scene held player 1 last frame
    bool m_crewFollowDue = false; // a follow waits for the scene camera to go
    std::uint64_t m_crewUpdates = 0;
    std::unique_ptr<camera::Cameras> m_cameras;    // player 1's; declared before the level, whose player holds them
    std::unique_ptr<scenes::SceneSystem> m_scenes; // the level's scenes, which outlive the level that hosts them
    scenes::SceneSystem* m_scenesBefore = nullptr; // the context's scene system before the level's, put back after
    // The level's glass panes and doors and what they ask of the world, before the level, which points them at its
    // collision mesh and path data.
    LevelObjectServices m_objectServices;
    world_objects::LevelObjects m_objects;
    std::unique_ptr<graphics::LevelLighting> m_lighting; // the level's, fresh for each (its scripts' lighting bindings)
    std::map<std::uint32_t, graphics::LightHandle> m_systemLights; // each particle system's light, by its serial
    // A particle system's sound emitter: its id in the game's sound and whether it is on.
    struct SystemEmitter {
        int id = -1;
        bool on = false;
    };
    std::map<std::uint32_t, SystemEmitter> m_systemEmitters; // each sounding system's emitter, by its serial
    std::unique_ptr<effects::LevelEffects> m_effects;        // before the level, which draws them
    std::unique_ptr<world_objects::Cars> m_cars; // the level's parked cars; before the level, which draws them
    world_objects::TriggerSpheres m_spheres;     // the level's trigger spheres (TriggerSphereCfg)
    world_objects::Radios m_radios;              // the level's radios (SetupRadio)
    world_objects::TagSpots m_tagSpots;          // the level's tag spots (CfgTagSettings)
    // A spray whose intro clip plays: the stick game goes live after it.
    struct TagIntro {
        double human = 0;
        double tag = 0;
        double flag = 0;
    };
    std::optional<TagIntro> m_tagIntro;
    std::optional<TagSession> m_tagSession; // player 1's spray under way (HuTag)
    // A cuffed human being freed by player 1's mash: the two handles.
    struct Uncuff {
        double freer = 0;
        double cuffed = 0;
    };
    std::optional<Uncuff> m_uncuff;
    const human::Human* m_uncuffHooked = nullptr; // the human the triangle hook is on
    double m_tagTicks = 0.0;                      // 60 Hz ticks not yet given to the tag spots
    world_objects::FlagNet m_flagNet;             // the level's flag network (FlagNetAddLink)
    std::optional<LevelPickups> m_pickups;        // over the context's spawn records and object types
    std::string m_shownPrompt;                    // the action prompt updateActionPrompt() last set
    std::optional<double> m_promptHintObject;     // the action object whose hint updateActionPrompt() queued
    std::string m_promptHint;                     // that hint
    std::unique_ptr<GameMode> m_level;
    std::uint32_t m_playerTeleports = 0;  // player 1's teleports the level has been told of
    PauseMode* m_pause = nullptr;         // what START pauses through; not owned
    std::vector<PlayOverlay*> m_overlays; // screens over play; not owned
};

} // namespace coney
