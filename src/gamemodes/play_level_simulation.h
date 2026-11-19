// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/dealer_goal.h"
#include "ai/route_planner.h"
#include "animation/anim_math.h"
#include "characters/anim_set.h"
#include "characters/character_choice.h"
#include "characters/character_types.h"
#include "characters/dynamic_clips.h"
#include "combat/stick.h"
#include "combat/throw_aim.h"
#include "core/error.h"
#include "core/interpolation.h"
#include "core/options.h"
#include "core/pad.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_pickups.h"
#include "graphics/throw_arc.h"
#include "hud/hud.h"
#include "hud/hud_owner.h"
#include "human/player.h"
#include "human/strike_shapes.h"
#include "human/target_human.h"
#include "warriors/created_humans.h"
#include "warriors/game_over.h"
#include "world/debug_camera.h"
#include "world/level_ground.h"
#include "world/view_frustum.h"
#include "world_objects/cars.h"
#include "world_objects/hats.h"
#include "world_objects/level_objects.h"
#include "world_objects/lock_pick.h"
#include "world_objects/loose_objects.h"
#include "world_objects/object_bodies.h"
#include "world_objects/object_tasks.h"
#include "world_objects/pickups.h"

namespace coney::script {
class SoundHost;
} // namespace coney::script

namespace coney {

/// What a play run has done so far: counts and the player's state, for the summary line.
struct PlayStats {
    std::uint64_t frames = 0;
    float travelled = 0.0F; ///< Metres the player's feet moved across the ground.
};

/// The level's simulation in play: player 1 on a ground (world::LevelGround: a level's or a sandbox's), the AI humans
/// (a level's scripts' cast, or a sandbox's fighters), the level's glass panes, doors, world objects, loose objects and
/// pickups as the humans meet them, the hats they wear, the props their clips make them carry, player 1's throw aim,
/// lock pick, mugging, stereo theft, flash and dealers' deals, the game-over check, and the HUD state those drive
/// (hud::HudOwner). It needs neither librw nor SDL, so a server can build it
/// (docs/guides/network-architecture.md#sub-missions); the platform's play mode (platform::PlayLevelMode, `coney
/// --play-level`) derives from it and adds the drawing and the streaming round the camera.
///
/// All of it is simulation state in the sense of docs/guides/network-architecture.md#checklist: it changes only in a
/// step, from the pads and the scripts, and nothing here reads the window, the picture or the frame rate. What the
/// platform draws it hears of through the hooks below: a human made (humanMade()), player 1's model changed
/// (changePlayerModel()), a character's files loaded (loadCastCharacter()); and it asks the platform whether a scene
/// holds a human (placeGround()). The step itself is the simulation's (update()); the presentation it interleaves (the
/// scenery's streaming round the camera, the lights, the glints, the rings, the HUD's sprites) is the presentation's
/// hooks, which do nothing by default, so the simulation steps alone where nothing is presented. The scenes are still
/// the platform's, reached through three hooks of their own until they move to the core (#1449).
///
/// The parts follow docs/research/objects.md, docs/research/characters.md#hats,
/// docs/research/combat-grabs.md#power-move-props, docs/research/crimes.md and docs/research/ai-scripting.md#dealer;
/// the class is Coney's own glue.
class PlayLevelSimulation : public GameMode, public ScriptedPlayer {
  public:
    ~PlayLevelSimulation() override;
    PlayLevelSimulation(const PlayLevelSimulation&) = delete;
    PlayLevelSimulation& operator=(const PlayLevelSimulation&) = delete;
    PlayLevelSimulation(PlayLevelSimulation&&) = delete;
    PlayLevelSimulation& operator=(PlayLevelSimulation&&) = delete;

    /// Player 1.
    [[nodiscard]] const human::Player& player() const { return *m_player; }
    /// The AI fighters.
    [[nodiscard]] const ai::AiHumans& fighters() const { return *m_ai; }
    /// The AI fighters, to steer: a disc test gives one an order as a script would.
    [[nodiscard]] ai::AiHumans& fighters() { return *m_ai; }
    /// Player 1's throw aim (docs/research/objects.md#throws).
    [[nodiscard]] const combat::ThrowAimState& throwAim() const { return m_throwAim; }
    /// The ribbon of player 1's throw-aim arc `alpha` of the way from the step before's arc to the newest (1 for the
    /// newest): what the arc's drawing draws, 36 vertices; empty while he does not aim
    /// (docs/research/graphics.md#throw-arc).
    [[nodiscard]] std::vector<graphics::ThrowArcVertex> throwArcRibbon(float alpha) const;
    /// Player 1's lock pick under way; null when none runs (docs/research/crimes.md#lockpick).
    [[nodiscard]] const world_objects::LockPick* lockPick() const { return m_lockPick ? &*m_lockPick : nullptr; }
    /// Player 1 holds the counts `gifts` give from the first step of play on (applied once, before that step's
    /// pickups): `--give`, Coney's test aid for captures that need an item.
    void giveAtStart(std::vector<ItemGift> gifts) { m_gifts = std::move(gifts); }
    /// The scenery's name: the level, or the sandbox layout's title.
    [[nodiscard]] std::string sceneName() const { return m_scenery->name(); }
    /// The level's parked cars (their stereos), or null without any.
    [[nodiscard]] const world_objects::Cars* cars() const { return m_cars; }
    /// How many AI humans there are in the world.
    [[nodiscard]] std::size_t fighterCount() const { return m_ai->liveCount(); }
    /// The character types the player can be made as (changeCharacter()), each with its model.
    /// @coney-only the debug menus' Change character list; the original has none
    [[nodiscard]] std::vector<characters::CharacterChoice> characterChoices() const;
    /// The character type the player is.
    [[nodiscard]] int playerType() const { return m_type; }
    /// The object of the prop `human` carries (its handle), made from his clip's event `0x30`; nothing when he carries
    /// none (docs/research/combat-grabs.md#power-move-props).
    [[nodiscard]] std::optional<double> carriedPropOf(const human::Human& human) const;
    /// ScriptedPlayer: what player 1 can act on for the action prompt: nothing during a mugging, a theft or a lock
    /// pick; else a held human to mug, a pickable door, a freed car stereo and a dealer's offer in reach.
    [[nodiscard]] PromptOffer promptOffer() const override;
    [[nodiscard]] bool combatFraming() const override { return m_player->combatFraming(); }
    /// ScriptedPlayer: `Human_ReleasePlayer` on the human the pad drives, with no one taking the pad
    /// (ai::AiHumans::releasePlayer()): he fights on as his class's brain. False with no cast or no player left.
    bool releasePlayerToAi() override;
    /// The model the player is drawn as.
    [[nodiscard]] const std::string& model() const { return m_model; }
    /// The HUD the mode steps and draws (its own until useHud()), for the HUD page and the scripts.
    [[nodiscard]] hud::Hud* hud() { return &m_hud->hud(); }
    /// Steps and draws `shared` (the game's HUD, which the scripts' bindings act on; it must outlive the mode) instead
    /// of the mode's own, with the player attached to its panel 0.
    /// @coney-only Coney's hand-over of the game's HUD to the level
    void useHud(hud::Hud& shared);

    /// The level's world objects (the scripts' spawn records brought in round the camera, the objective markers):
    /// what the newest step draws (play_level_world.cpp).
    [[nodiscard]] const world_objects::ObjectTasks& worldObjects() const { return m_objectTasks; }
    /// The parked cars' hit effects of the newest step (world_objects::Cars::takeHitEffects()), in order: the sparks,
    /// the windows' shatters with their glass sound, the bonnet's steam (docs/research/cars.md#hit-effects).
    [[nodiscard]] const std::vector<world_objects::CarHitEffect>& carHitEffects() const { return m_carHitEffects; }
    /// One object in a human's hand as a frame draws it: who holds it, its handle, the pose bone it hangs from and its
    /// world pose (game axes).
    struct HeldDraw {
        const human::Human* holder = nullptr;
        double handle = 0.0;
        std::size_t bone = 0;
        world_objects::WorldPose pose;
    };
    /// The objects in every human's hand at the newest step, player 1's and each AI human's, posed by his own hand
    /// bone as render() places them (docs/research/objects.md#given-weapon): a disc test's way to ask what is drawn in
    /// a hand, whatever the renderer. An object that is not a spawn record of a type with a pick-up clip's take event
    /// is left out, as the draw leaves it out.
    /// @coney-only a disc test's view of what is drawn in the hands
    [[nodiscard]] std::vector<HeldDraw> heldDraws() const;
    /// How many objects made as `typeName` the level's spawn records still hold (not removed): a disc test's way to ask
    /// whether the boards a broken fence threw are still standing, whatever the renderer.
    /// @coney-only a disc test's count of standing objects
    [[nodiscard]] std::size_t standingObjectsOf(std::string_view typeName) const;
    /// One step of the level (docs/guides/network-architecture.md#sub-presentation): the humans, the objects, the HUD's
    /// state and the game-over check in the original's order, with the presentation's hooks below called where its
    /// parts of the step fall (the scenery streamed round the step's camera, the glints, the HUD's sprites, the rings).
    /// Draws nothing.
    /// @coney-only Coney's order of the level's parts in a step; each part carries its own @orig
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// Player 1's statistics as the play mode counts them.
    [[nodiscard]] const PlayStats& stats() const { return m_stats; }
    /// The sandbox's targets (none in a level).
    [[nodiscard]] std::span<human::TargetHuman* const> targets() const { return m_targetPointers; }

    /// How many doors and barriers within `radius` of `at` would be drawn now (the doors' draw list, hidden and removed
    /// ones left out): a disc test's way to ask whether a broken fence is still on screen.
    /// @coney-only a disc test's count of the doors drawn
    [[nodiscard]] std::size_t doorsDrawnNear(anim::Vec3 at, float radius) const;

  protected:
    /// The simulation on `ground` (owned from now on) with player 1 playing `character` (the scene's character, which
    /// the sandbox's targets and fighters play too) as model `model`, at the ground's start (dropped onto it when
    /// `snapToGround`), made as character type `type` of `types` (human::playerClassOf()). `wad` (which must outlive
    /// the simulation) gives the scripts' dynamic clips; `print` receives the run log's lines.
    PlayLevelSimulation(const io::Wad& wad, std::unique_ptr<world::LevelGround> ground,
                        std::unique_ptr<human::PlayerCharacter> character, std::function<void(std::string_view)> print,
                        std::string model, const characters::CharacterTypes& types, int type, bool snapToGround);

    // --- The platform's hooks: what the simulation tells the presentation, and the one thing it asks of it.

    // The ground a placement of `human` drops onto (human::Human::place()): the level's collision, or null while a
    // scene holds the human (the scenes are the platform's yet).
    [[nodiscard]] virtual const raycast::CollisionMesh* placeGround(double human) const = 0;
    // The character `model` names, loaded from the disc for the simulation, with what the platform keeps to draw it
    // (its textures). Fails as the character loader does.
    [[nodiscard]] virtual std::expected<std::unique_ptr<human::PlayerCharacter>, Error>
    loadCastCharacter(std::string_view model) = 0;
    // An AI human was made, playing characterOf() it, as model `model` (empty: the scene's character): the platform
    // makes what draws him.
    virtual void humanMade(const human::Human& human, const std::string& model) = 0;
    // Player 1 is drawn as `model` from now on (HuChangePlayerGang's hand-over to a human of another model): the
    // platform loads it and swaps his character, or keeps his own with a line when it does not load.
    virtual void changePlayerModel(const std::string& model) = 0;

    // --- The presentation's hooks: the parts of the step a presentation makes (the platform's streaming, lights,
    // glints, rings and HUD sprites; docs/guides/network-architecture.md#sub-presentation). Each does nothing by
    // default, so the simulation steps alone where nothing is presented (a test, a server).

    /// The step's camera as the presentation streamed the scenery round it: its pose (RenderWare's axes) and the
    /// scenery's draw distance, within which the world objects are brought in.
    struct StepView {
        world::CameraPose pose;
        float drawDistance = 0.0F;
    };
    // `--freeze-world` once frozen: the presentation's whole step (the scenery and the world objects round the camera),
    // returning true; false (the default) when the world advances.
    virtual bool stepFrozenWorld(const FrameTime& /*frame*/) { return false; }
    // After the humans' step and before the scenes': the step's traces (`--trace`, `--trace-humans`).
    virtual void traceStep() {}
    // The scenery's step round the step's camera (its streaming and draw distance), the lights' step and the
    // visibility pass the next step's streaming reads; returns that camera. The default: player 1's camera with its
    // far clip as the draw distance, nothing streamed.
    // @coney-only Coney's presentation hook; the original streams round its camera inside its level update
    virtual StepView stepScenery(const FrameTime& frame);
    // The glints' step, `ticks` 60 Hz ticks of their blink, after the world objects' step
    // (docs/research/particles.md#glints).
    virtual void stepGlints(int /*ticks*/) {}
    // Where this step's world objects and doors are, for the frames to blend from (after the world objects' step).
    virtual void recordDrawnPoses() {}
    // The HUD's update ran: its sprites for the step and the spinning icons' poses.
    virtual void hudStepped() {}
    // The health rings' step after the HUD's, with player 1's pad (docs/research/hud-radar.md#the-health-rings).
    virtual void stepRings(const Pad& /*pad*/, std::uint64_t /*nowMs*/) {}
    // The step's end (`--freeze-world` freezes the world here).
    virtual void stepEnded(const FrameTime& /*frame*/) {}
    // The scenes, the platform's until they move to the core (#1449): whether a scene holds `human` (a script handle),
    // one step of them at `nowMs` with the pads' `buttons`, and whether a scene's letterbox is in or moving at `nowMs`.
    [[nodiscard]] virtual bool sceneHolds(double /*human*/) const { return false; }
    virtual void stepScenes(std::uint64_t /*nowMs*/, std::uint16_t /*buttons*/) {}
    [[nodiscard]] virtual bool letterboxShown(std::uint64_t /*nowMs*/) const { return false; }

    // Whether a scene holds player 1 now (sceneHolds()): his pad does nothing and he is drawn as the scene poses him.
    // @coney-only Coney's question to the scenes about player 1
    [[nodiscard]] bool sceneHoldsPlayer() const;

    // --- play_level_step.cpp: the parts of the step that are not another file's.

    // The world objects' step round the step's camera `view` (the scripts' spawn records brought in within its draw
    // distance), `elapsedMs` of game time, then the glints' (stepGlints()).
    // @coney-only Coney's wiring of the world objects' step to the step's camera
    void stepWorldObjects(const StepView& view, std::uint32_t elapsedMs);
    // Gives each human in the step the clip replacements its script state names (`HuUseAnim`, `HuUseAnyAnim`), when
    // they changed: the idle's through Human::setIdleClip(), the others' through Human::setOverrideClip().
    // @coney-only Coney's application of the scripts' recorded clip replacements, once a step
    void applyAnimOverrides();
    // Makes the ground's targets (a sandbox layout's `target` lines), dropped onto the ground.
    // @coney-only the sandbox's passive targets; the original has none
    void makeTargets();

    // The character the player plays: his own after a change of character, else the scene's.
    [[nodiscard]] const human::PlayerCharacter& playerCharacter() const {
        return m_playerCharacter ? *m_playerCharacter : *m_character;
    }
    // The character AI human `human` plays: the one he was made as (or the Entity inspector gave him), else the
    // scene's.
    // @coney-only Coney's record of the character a human plays
    [[nodiscard]] const human::PlayerCharacter& characterOf(const human::Human& human) const;

    // --- play_level_cast.cpp: the level's scripts' humans, and the AI fighters.

    // The handle the scripts name a cast AI human by (its brain's); 0 for one with no brain.
    // @coney-only Coney's lookup of a cast human's script handle
    [[nodiscard]] double castHandleOf(const ai::AiHuman& fighter);
    // The cast AI human the scripts name `handle`; null when none is.
    // @coney-only Coney's lookup of a cast human by his script handle
    [[nodiscard]] const ai::AiHuman* castHumanOf(double handle);
    // Spawns a fighter dropped onto the ground below `spot`, with its mesh, as the character of `model` (empty: the
    // scene's own).
    // @coney-only the sandbox's and the debug menus' fighters
    void addFighter(anim::Vec3 spot, float headingDegrees, const std::string& model = {}, bool debugSpawned = false,
                    ai::FighterSide side = ai::FighterSide::Enemy);
    // The level's scripts' humans as AI humans: the AI on the scripts' brains, then the scripts' hold released with
    // castHuman() as its spawner, which makes the humans created so far and replays the calls on them.
    void makeCast(const ScriptedCast& cast, const ai::AiConfig& fighters);
    // The level's route planner on the ground's path data (none without), with the ground's collision as its probe:
    // the brains plan their moves on it.
    // @coney-only Coney's wiring of the route planner to the ground
    void makePlanner();
    // The AI fighters of a level without the scripts' cast: the ground's `fighter` places (a sandbox layout's) as
    // `fighters` configures them, planning on the route planner (makePlanner() first).
    // @coney-only the sandbox's fighters; a level's are its scripts' cast (makeCast())
    void makeLayoutFighters(const ai::AiConfig& fighters);
    // One human the scripts created, made: player 1's first creation is the player (his brain bound to it); any other
    // with a position an AI human of its type's class, drawn as its model, snapped as `HuCreate` snaps. Returns its
    // brain; null for a creation with no position. The script's half (name, slot, handle) is makeHuCreate's.
    // Research: docs/research/characters.md#creation
    // @orig 0x00233d60 Human_Create (unknown)
    ai::Brain* castHuman(const HumanCreation& human);
    // HuChangePlayerGang's hand-over to `to` (a cast human): the player is moved, drawn and named as it, and it leaves.
    // The gang's pick is ai::ScriptedBrains::changePlayerGang()'s. Research:
    // docs/research/characters.md#level99-handover
    // @orig 0x00239b80 Players_ChangeGang (unknown)
    void takePlace(const ai::Brain& to);
    // The brain type a human of character type `type` fights as: its class's, as castHuman() makes it.
    [[nodiscard]] ai::BrainType castBrainType(int type) const;
    // The character `model` names, loaded once for every human of it (loadCastCharacter()); the scene's character when
    // it is empty or fails to load.
    // @coney-only Coney's cache of the characters it loads, by model
    const human::PlayerCharacter& castCharacter(const std::string& model);
    // Gives a fighter the debug menus spawned, playing `character` as `model`, the character type it is drawn as (its
    // voice set) and, in a level with scripts, a handle of its own in the scripts' hold, so that gameplay hands its
    // sounds to the game's sound as it does a script's human's (the original plays every human's clip sounds,
    // docs/research/sound.md#anim-sounds).
    // Research: docs/guides/debug-menu.md#pages (AI fighters)
    // @coney-only debug tooling: the original has no spawner
    void bindDebugFighter(human::Human& made, const human::PlayerCharacter& character, const std::string& model);
    // Takes the handles of the fighters the debug menus spawned out of the scripts' hold, before their removal.
    // Research: docs/guides/debug-menu.md#pages (AI fighters)
    // @coney-only debug tooling: the original has no spawner
    void unbindDebugFighters();

    // --- play_level_objects.cpp: the level's glass panes and doors.

    // Points `objects` (the scripts' cast's, may be null) at the scenery's collision mesh and path data.
    // @coney-only Coney's wiring of the scripts' objects to the scenery
    void bindObjects(world_objects::LevelObjects* objects, const script::RecordedCalls* recorded);
    // The lock pick before the player's step: triangle at a pickable door starts one; while one runs the dial turns,
    // cross judges a pin and triangle or another button abandons. Returns whether it holds the pad this step.
    bool stepLockPick(const Pad& pad);
    // After the player's step: his object attack's hit sent to its object, then the objects' two 60 Hz ticks.
    void stepObjects();
    // Player 1's object attack under way (Player_ObjectAttack's clip, 661 or 662, with its object): whether its hit
    // lands on the object this step. It lands when the clip's strike shapes come on (a car or other object) or strike
    // it (a pane or door, through strikeObjects()), not at the attack's start
    // (docs/research/combat-targets.md#breakables).
    // @orig 0x0021b290 Strike_Contact (unknown)
    [[nodiscard]] std::optional<double> objectAttackLands(const human::Human& human);
    // The strike test's objects (`Strike_Contact`'s object branch, docs/research/combat/index.md#moving-strikes):
    // `human`'s posed strike `shapes` strike each door or barrier whose enabled triangles they touch, each pane whose
    // body they reach and each strike-target world object (a street prop) whose body (props.h bodyTouches()) they
    // reach, once while the shapes stay on, with message 1 of the attack record's hit kind, each sounding as its type's
    // material; then the level (strikeLevel()). **Coney's stand-ins**: a car's hit is not made here; only the drawn
    // world objects are tested.
    void strikeObjects(human::Human& human, std::span<const human::PosedShape> shapes);
    // The strike shapes' contact with the level mesh: once while the shapes stay on, the fist (or the charging body)
    // against the triangle met, as `human`'s sound (`player`: he is player 1; docs/research/sound-events.md).
    void strikeLevel(human::Human& human, std::span<const human::PosedShape> shapes, bool player);
    // A strike from `attacker` of `kind` on the world object `handle` of `type` at `point`, which stands at `pose` (the
    // prop's `Strike_Contact`, world_objects::Props::strike()), printed as an `objects:` line.
    // `runIn`: the attacker ran into it (a sprint into a RUNTARGET body).
    void strikeProp(double attacker, double handle, const world_objects::ObjectType& type, world_objects::HitKind kind,
                    anim::Vec3 point, anim::Vec3 direction, anim::Vec3 attackerAt, const world_objects::PropPose& pose,
                    bool runIn = false);
    // The handle `human` goes by as an attacker: player 1's, its brain's, else none.
    // @coney-only Coney's lookup of a human's script handle
    [[nodiscard]] double handleOf(const human::Human& human) const;
    // Whether a ray from `from` to `to` meets the level's collision (the panes' and doors' among it): the pick-up
    // search's and the walk-over's sight test.
    // @coney-only Coney's sight ray through its collision world
    [[nodiscard]] bool sightBlocked(anim::Vec3 from, anim::Vec3 to) const;
    // Player 1 walks over the power-ups he touches (LevelPickups::walkOver(), which plays their pick-up sounds).
    void walkOverPowerups();
    // A strike on a world object or a car reports a 30 m noise (`AI_ReportNoise`, `0x002936a8`;
    // docs/research/combat/index.md, cars.md#hit-effects). Coney's AI does not hear it yet; the noise stamps the
    // ambient manager's event, which opens the `_DAM_` emitters' window (docs/research/sound.md#ambient).
    // @orig 0x002936a8 AI_ReportNoise (unknown)
    void reportNoise();
    // Gives player 1 triangle's pick-up over `pickups` (may be null: none): the search, with sight rays through the
    // level's collision, starts the pick-up on him.
    // @coney-only Coney's wiring of the pick-up search to the level's power-ups
    void bindPickups(LevelPickups* pickups);
    // The car whose freed stereo is in reach of feet at `feet` (a kind-3 context record), nearest first; null for none.
    [[nodiscard]] const world_objects::Car* stereoInReach(anim::Vec3 feet) const;
    // The pickable door nearest feet at `feet` within reach (a kind-2 context record); null for none.
    [[nodiscard]] const world_objects::Door* lockInReach(anim::Vec3 feet) const;
    // Player 1's triangle at a dealer offering a deal (a kind-4 context record, docs/research/ai-scripting.md#dealer):
    // the first whose offer is within ai::kDealReach in plan and whose feet are within the prompt height of the human's
    // waist deals. Returns whether one took the press.
    bool tryDeal(human::Human& human);
    // Player 1's triangle beside a Warrior whose swap prompt is up (docs/research/ai-brain-types.md#warrior-swap): the
    // Warriors ahead within 1.5 m are asked nearest first; the first that does not refuse turns to him over 0.2 s and
    // the two swap what they hold at once, and player 1 says `give_me` when he now holds something. Returns whether one
    // took the press. **Coney's stand-in**: the two-player swap is not built.
    // @orig 0x00306040 WarriorBrain_OnPrompt (unknown)
    bool trySwap(human::Human& human, const world_objects::SightBlocked& blocked);
    // A deal's outcome for player 1: the item and money moved for a sale or a rip-off, and the dealer's line.
    void settleDeal(ai::Brain& brain, ai::DealerGoal& dealer, ai::DealOutcome outcome);
    // Each dealer whose money clip (`money_take`, his 668) passed its action event completes the sale its pair
    // started (`DealerGoal_FinishPair`, docs/research/ai-scripting.md#dealer-buy).
    void stepDealerPairs();
    // The game-over check each update (GameState_CheckGameOver, docs/research/combat-reactions.md#defeat) with player 1
    // as it sees him, then the failure's hand-off: the HUD hidden at its first update, and after 180 updates (10 once
    // cross is newly pressed) the mission-failed mode with the reason's `GSTRING.HUD` title. **Coney's stand-ins**: a
    // player at 0 health counts as knocked out and none as dead; no player frees himself (Coney has no uncuffing);
    // `+0x414` never blocks the wait; a held member is one attached to his holder; the route check is a route request
    // (no blocked links in Coney); the failed camera, the tint, the blur and the music's fade are not built.
    // @orig 0x004197a8 GameState_CheckGameOver (unknown)
    // @orig 0x00169dd8 Gang_NoneAbleToHelp (unknown)
    // @orig 0x00169ea0 Gang_CanReachToHelp (unknown)
    void stepGameOver(const Pad& pad);
    // Player 1's flash (command 0x28, d-pad right, docs/research/combat-rage.md#flash): with a flash carried and health
    // below its maximum, refused only when down or dead, cuffed, airborne or climbing: 665 SPECIAL_FLASH plays when
    // nothing holds him, and its action event 25 updates in uses the flash (a clip cut first spends nothing); when
    // something holds him (a grab, which stays; a knockdown, a stun, a hit reaction) the flash is used at once, unless
    // 665 already plays. **Coney's stand-in**: the full-health rage use (upgrade (6, 8)) is not built.
    // @orig 0x002843f8 Player_UseItemCommand (unknown)
    void stepFlash();
    // `Flash_Use`: one flash spent, interface sound 23 (`vags/misc/flash`), the health filled and wounding ended, and
    // the HUD's ring request. **Coney's stand-in**: always the full heal (Coney has no difficulty setting; above
    // normal the original heals a player by half), and the player's screen effect reset is not built.
    // @orig 0x00284280 Flash_Use (unknown)
    void useFlash(human::Human& human);
    // Player 1's square may strike the world objects that are strike targets, the cars he faces and the level's whole
    // glass panes, aiming at their positions, aim points and centres.
    void giveObjectTargets();
    // The type of the world object `handle` (a spawn record) when its body makes it a strike target; null otherwise.
    // @coney-only Coney's lookup of a world object's strike-target type
    [[nodiscard]] const world_objects::ObjectType* worldObjectType(double handle) const;
    // The bodies of the world objects in the world this step (their types' CfgObj shapes at their poses), and the
    // walking humans' slide along the BLOCKHUMANS ones (docs/research/physics.md#bodies). First the RUNTARGET bodies
    // running humans met last step take their strike (Human_OnContact, docs/research/objects.md#trash-props).
    // @orig 0x00391d48 Obj_CreatePhysicsBody (unknown)
    void stepObjectBodies();
    // Player 1's pick-up that reached its clip's event this step: the object is taken. The downed humans' drops come
    // first (stepDownDrops()).
    // @coney-only the play mode's per-step glue of the pick-up, the throw, the mugging and the drops
    void stepPickups();
    // The drops of the humans whose health ran out this step (Human::fighter()'s takeHealthOut()): player 1's, then
    // the AI humans' (docs/research/crimes.md#down-drop).
    // @orig 0x002674c0 Human_OnHealthOut (unknown)
    void stepDownDrops();
    // `human`'s drop, when his health ran out this step: nothing with flag 0x4 or already wounded; otherwise what
    // he, his brain and his class hold goes to LevelPickups::dropCarried(), and his money and carried object follow
    // it. `player` is his player (0 for player 1), -1 for an AI human.
    // @orig 0x002674c0 Human_OnHealthOut (unknown)
    void dropOnHealthOut(human::Human& human, int player);
    // A take from `object` when it is a weapon pile (world_objects::Props::takeFromPile()): the object made for the
    // hand, kNoObject when none was made; nothing when `object` is no pile, or a pile taken itself.
    [[nodiscard]] std::optional<double> takeFromPile(double object);
    // Player 1 lets go of `held`: it leaves the hand from where the hand holds it and falls (dropHeld()), or, `thrown`,
    // flies off with the throw's velocity and spin (docs/research/objects.md#held).
    // @orig 0x00257f38 Human_DropHeld (unknown)
    // @orig 0x002586d8 Human_ReleaseThrow (unknown)
    // @orig 0x003fe6b8 MeleeWeapon_Detach (unknown)
    void dropHeld(human::Human& human, double held, bool thrown = false);
    // What the run log says of the object `handle` a player holds or lets go: its type name, its kind (`CfgObj`'s
    // objectKind) and its hold style (the anim set it gives the hand), as ` (type NAME, kind K, hold style S)`; empty
    // when its type is unknown (docs/guides/run-log.md).
    // @coney-only the run log's text for a held object
    [[nodiscard]] std::string heldObjectLogText(double handle) const;
    // The loose objects in flight: their fall, bounce and settle (docs/research/physics.md#settle), their poses written
    // back into their spawn records, each level impact's sound (impactSound()); an object that wears out on a contact
    // breaks (flyingBreak()).
    void stepLooseObjects();
    // A flying object's level contact that did not break it (docs/research/sound-events.md#objects): its material
    // against the surface's at 1 / (n + 1), for its first three impacts above 2 m/s.
    // @orig 0x003939a8 WorldObject_OnImpact (unknown)
    void impactSound(double handle, const world_objects::LooseImpact& impact);
    // A flying object's break at its contact (docs/research/objects.md#throws): the impact's material pairs on the
    // level mesh, message 6 from the boxes its thrower stands in, then its class's break: a `thrown_weapon` at the hit
    // point with its dust and glass, gone at the next step; an `overhead_weapon` or a cash register through
    // Props::breakFlying().
    // @orig 0x003939a8 WorldObject_OnImpact (unknown)
    // @orig 0x00393e20 WorldObject_Break (unknown)
    // @orig 0x004034b8 ThrownWeapon_Break (unknown)
    void flyingBreak(double handle, const world_objects::LooseObject& object);
    // Player 1's mugging: the scripts' record for the next one, and the end of one (the money, the mug callback).
    void stepMugging(human::Human& human);
    // The stereo-theft panel follows player 1's theft: shown from the press, its progress each update with cue 0x22 per
    // stage (and 0x23 for the last), gone as the end clip starts (docs/research/hud-functions-panels.md#stereo-layout).
    void stepStereoPanel(const human::Human& human);
    // The mug meter follows player 1's mugging: shown while it runs, with the raw left stick, whether it is on target
    // and both bars' fills (docs/research/hud-functions-panels.md#mug-meter-layout).
    void stepMugMeter(const human::Human& human, const Pad& pad);

    // --- play_level_throw_aim.cpp: the throw's aim.

    // Player 1's throw aim (docs/research/objects.md#throws): L1 with a set 5 object enters it, `pad`'s raw left stick
    // turns and pitches it, the arc is traced through the level each frame and what it meets is kept for HuIsAimingAt;
    // L1 again, the object lost, a grab or the pad lost leaves it. **Coney's stand-ins**: the sweeps are sampled along
    // the arc's segments, humans are upright capsules, a human target's point is a fixed head height.
    // @orig 0x0027da10 Player_OnL1Pressed (unknown)
    void stepThrowAim(const Pad& pad);
    // The lock-pick dial follows player 1's pick: shown at its difficulty while it runs, with the pins' angles and the
    // pin the next press judges (docs/research/hud-functions-lua.md#lock-pick-dial-layout).
    void stepLockPickDial();
    // Player 1's handle, as the scripts know him; the nil handle without a cast.
    // @coney-only Coney's lookup of player 1's script handle
    [[nodiscard]] double playerHandle() const;

    // --- play_level_held.cpp: what the humans hold.

    // Where object `held` hangs in player 1's hand at `snapshot`'s pose; nothing when it is not a record in the hand
    // with a pick-up clip's take event (docs/research/objects.md#held).
    // @coney-only Coney's lookup of the held object's hand pose for player 1
    [[nodiscard]] std::optional<world_objects::WorldPose> heldPose(double held,
                                                                   const human::PlayerSnapshot& snapshot) const;
    // Where object `held` hangs in `human`'s hand, `character` posed by `pose` at `feet` facing `heading`: the bone
    // and offset of its type's pick-up clip's take event in his own animations, at his scale. Nothing when it is not a
    // record of a type with such an event (docs/research/objects.md#given-weapon).
    [[nodiscard]] std::optional<HeldDraw> heldDrawOf(double held, const human::Human& human,
                                                     const human::PlayerCharacter& character, const anim::Pose& pose,
                                                     anim::Vec3 feet, float heading) const;

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
    // After the humans' step: every human's hat put on, replaced or knocked off, and the falling hats moved (the
    // hat object's update). Research: docs/research/script-types.md#hat-object
    // @orig 0x003e7b88 HatObject_Update (unknown)
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

    // --- play_level_props.cpp: the props the humans' clips make them carry
    // (docs/research/combat-grabs.md#power-move-props).

    // One human's carried prop as the mode keeps it: its object (0 for none), type and where it hangs.
    struct Carrier {
        double prop = 0.0;
        std::string typeName;
        world_objects::HeldAttachment local;
    };
    // After the humans' step: every human's carried prop made, hung from its attach point or removed.
    // @coney-only Coney's per-frame loop over the level's humans; each one's step carries its @orig
    void stepCarriedProps();
    // One human's part of stepCarriedProps(), posed at this step by `pose` of `character` at `feet` facing `heading`.
    void stepCarrier(human::Human& human, const human::PlayerCharacter& character, const anim::Pose& pose,
                     anim::Vec3 feet, float heading);
    // Makes the object of `prop` at `human` and attaches it at point 0x13 with its type's held offset, as `carrier`'s;
    // a type that is missing or not one to carry is not made, and the human carries nothing.
    // @orig 0x0024cbb0 Human_CreateCarriedProp (unknown)
    // @orig 0x0024cd30 Human_AttachCarriedProp (unknown)
    void makeCarriedProp(human::Human& human, const human::CarriedProp& prop, Carrier& carrier);
    // Removes `carrier`'s object.
    void removeCarriedProp(Carrier& carrier);
    // Removes the object of a prop `human` no longer carries now, not at the next stepCarriedProps(): a scene bind's
    // reset releases it in the middle of the step, and the original's release destroys the object at once.
    void dropReleasedProp(const human::Human& human);

    const io::Wad& m_wad;
    // The scripts' dynamic clips (HuUseAnim's idle replacements), loaded from the disc when first named.
    characters::DynamicClips m_dynamicClips{
        [this](std::string_view name) { return characters::loadAnimResource(m_wad, name); }};
    std::unique_ptr<world::LevelGround> m_scenery;       // the ground the humans stand on: the platform's scenery
    std::unique_ptr<human::PlayerCharacter> m_character; // the scene's: the targets and fighters play it
    // After a change of character, the player's own character; and the characters he played before, kept while the
    // mode lasts because a target may still be playing a paired clip from one of their anim sets.
    std::unique_ptr<human::PlayerCharacter> m_playerCharacter;
    std::vector<std::unique_ptr<human::PlayerCharacter>> m_retired;
    // The Animation player's other characters' anim sets, by model (src/platform/play_level_anim.cpp): loaded when
    // first opened and kept while the mode lasts, declared before the player, whose tasks may hold their clips.
    struct AnimSetCharacter {
        std::unique_ptr<human::PlayerCharacter> character;
        std::unique_ptr<characters::AnimSet> own; // its own clips only
    };
    std::map<std::string, AnimSetCharacter, std::less<>> m_animSetCharacters;
    std::unique_ptr<characters::AnimSet> m_genericAnims; // the generic character data alone, made when first opened
    std::string m_animSetName;                           // the set and the clip the Animation player last played
    std::string m_animClipName;
    characters::CharacterTypes m_types; // the configuration's types
    int m_type = 0;                     // the type the player is
    int m_levelNumber = 0;              // the scene's level number (its models), 0 for a sandbox
    std::unique_ptr<human::Player> m_player;
    std::function<void(std::string_view)> m_print;
    std::string m_model; // the Character List model the player is
    // The characters the scripts' humans and the fighters play, by model (null: it did not load), before the AI so
    // they outlive the humans playing them.
    std::map<std::string, std::unique_ptr<human::PlayerCharacter>, std::less<>> m_castCharacters;
    ScriptedCast m_cast;            // the level's scripts' humans and brains; all null without them
    bool m_castPlayerBound = false; // whether player 1's creation is bound to the player
    // The level's route planner (null without path data), before the AI so it outlives the brains that use it.
    std::unique_ptr<ai::RoutePlanner> m_planner;
    // The AI fighters.
    std::unique_ptr<ai::AiHumans> m_ai;
    // The character each AI human plays and the model he was made as (empty: the scene's character), by human: the
    // simulation's half of what the platform's meshes draw. Keyed by the human, as m_wearers and m_carriers are,
    // because humans have no stable id yet; the pointer is safe while AiHumans erases no human before teardown.
    // Replication will rekey all three by a human id (network-architecture.md#checklist, ids not pointers).
    struct Played {
        const human::PlayerCharacter* character = nullptr;
        std::string model;
    };
    std::map<const human::Human*, Played> m_played;
    double m_playerHandle = 0.0; // player 1's handle in the scripts and the scenes
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
    std::vector<world_objects::CarHitEffect> m_carHitEffects; // the cars' hit effects of the newest step
    GameOverCheck* m_gameOver = nullptr;                      // the game-over check (GameState::gameOver); not owned
    const std::array<PadOptions, 2>* m_padOptions = nullptr;  // the pads' options (GameState::saved.pads); not owned
    std::function<void(std::string_view)> m_missionFailed;    // opens the mission-failed mode with a title
    bool m_failShown = false;                                 // the failure's first hand-off update ran
    bool m_wasMugging = false;                                // player 1 was mugging at the last step
    std::optional<bool> m_mugEnding;                          // a decided mugging's result, until its end clip finishes
    std::uint32_t m_mugEndClip = 0;                           // that end clip
    LevelPickups* m_pickups = nullptr;                        // the level's loose objects for the pick-up; not owned
    int m_theftStage = 0;                                     // the theft's stage the panel last showed
    std::optional<world_objects::LockPick> m_lockPick;
    int m_lockPickDifficulty = 0;
    bool m_flashRingRequest = false;      // a flash used: the HUD's ring request (docs/research/hud.md)
    script::SoundHost* m_sound = nullptr; // the game's sound for the dealers' lines; not owned
    std::vector<ItemGift> m_gifts;        // `--give`'s counts, until the first step's pickups
    // The world objects: the scripts' records and types (null without a cast) and their objects.
    world_objects::SpawnRecords* m_records = nullptr;
    const world_objects::ObjectTypes* m_objectTypes = nullptr;
    world_objects::ObjectTasks m_objectTasks;
    world_objects::ObjectBodies m_objectBodies; // this step's world-object bodies
    combat::ThrowAimState m_throwAim;           // player 1's throw aim
    std::vector<anim::Vec3> m_previousArc;      // the arc's points on the step before (none: not aiming)
    // The RUNTARGET bodies running humans met last step: (the human's handle, his feet, the object), struck next step.
    std::vector<std::tuple<double, anim::Vec3, double>> m_runContacts;
    world_objects::LooseObjects m_looseObjects;  // the dropped objects falling or settling
    std::vector<double> m_brokenThrown;          // thrown objects broken last step, removed this one
    combat::CombatRandom m_throwRandom{0x7417U}; // a thrown object's spin (a fixed seed)
    // The hats: the fittings (read once from the recorded `CfgHat` calls), each human's, the handles worn, those
    // falling, and where this frame draws the worn ones.
    world_objects::HatFits m_hatFits;
    bool m_hatFitsRead = false;
    std::map<const human::Human*, Wearer> m_wearers;
    std::set<double> m_wornHats;
    world_objects::FallingHats m_fallingHats;
    // The carried props: each human's and the handles carried.
    std::map<const human::Human*, Carrier> m_carriers;
    std::set<double> m_carriedProps;
    // The HUD the steps drive: the simulation's own, or the game's after useHud().
    std::unique_ptr<hud::HudOwner> m_hud = std::make_unique<hud::HudOwner>();
    PlayStats m_stats;
    std::uint32_t m_lastAnimId = 0; // player 1's clip at the last step, for the run log's clip lines
    bool m_frozen = false;          // player 1 frozen (he ignores the pad)
    // The free camera at the last two steps, while it is on (it takes pad 1 and player 1 stands still).
    std::optional<Interpolated<world::DebugCamera>> m_freeCamera;
    // The sandbox's targets, stepped after the player; what draws them is the platform's.
    struct Target {
        std::unique_ptr<human::TargetHuman> human;
        bool frozen = false;                               // the Entity inspector's freeze: its step is skipped
        const human::PlayerCharacter* character = nullptr; // the character it is skinned as; null: the scene's
        std::string model;                                 // the model it is drawn as; empty: the scene's
    };
    std::vector<Target> m_targets;
    std::vector<human::TargetHuman*> m_targetPointers;
    std::vector<human::Combatant*> m_combatants; // the targets, as the player's step takes them
};

} // namespace coney
