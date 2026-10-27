// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/anim_set.h"
#include "combat/anim_ranges.h"
#include "combat/commands.h"
#include "human/body.h"
#include "human/carried_prop.h"
#include "human/climb.h"
#include "human/combatant.h"
#include "human/face.h"
#include "human/fighter.h"
#include "human/holdable.h"
#include "human/human_animator.h"
#include "human/human_contacts.h"
#include "human/human_flags.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"
#include "human/script_state.h"
#include "human/stamina.h"
#include "human/strike_shapes.h"
#include "human/target_human.h"
#include "human/victim.h"
#include "physics/mesh_sweep.h"
#include "raycast/collision_mesh.h"

// A human driven through its per-player record, which a pad (the player) or a brain writes: its locomotion,
// sprint and stamina, jump and climbs, its fighting (Fighter), its standing on and falling from the level's collision
// mesh, and its animation, stepped at the characters' fixed 30 Hz in the original's three passes (animation, state
// update, actions; human/humans.h runs them across every human). Platform-neutral and deterministic: no clock, no
// randomness, so a scripted pad gives the same path on every run (test mode).
// Research: docs/research/characters.md, docs/research/tasks.md#humans-update

namespace coney::human {

/// How a flying human moves this update (Human::setFlight(), the debug menu's noclip; Coney's own tool, the original
/// has none): the left stick flies it across the ground as it walks it, turned by the camera, at `speed` for a full
/// push, and it rises at `climb` (negative: descends).
struct Flight {
    float speed = 0.0F; ///< Metres a second across the ground with the stick pushed fully.
    float climb = 0.0F; ///< Metres a second upward; negative descends.
};

/// What the human is given each update: the left stick, the camera's view direction, which turns it, the two
/// buttons traversal reads, and combat's command, buttons and targets.
struct HumanInput {
    float stickX = 0.0F;        ///< Left stick, -1 (left) to 1 (right), after the pad's own dead zone.
    float stickY = 0.0F;        ///< Left stick, -1 (down) to 1 (up).
    anim::Vec3 cameraForward;   ///< The camera's view direction; only its part across the ground is used.
    bool sprintHeld = false;    ///< L2 is held: asks for a sprint (docs/research/characters.md#sprint).
    bool actionPressed = false; ///< Triangle went down this update (command 10): a climb, an action or a jump.
    combat::CommandId command = combat::command::kNone; ///< This update's command (combat::CommandMatcher).
    std::uint16_t buttons = 0;                          ///< The held buttons (the block reads R1).
    std::span<Combatant* const> targets{};              ///< The humans that can be fought.
    /// Of `targets`, those the fight stance never takes (no threat, or a pad player). Defaulted, like `targets`, so a
    /// designated initializer may leave it out (GCC's and Clang's -Wmissing-field-initializers).
    std::span<Combatant* const> calm{};
};

/// A brain's move (brain `+0x110`, `+0x114`, `+0x90`): the heading and speed the locomotion follows in place of the
/// stick for a human a brain drives, and the point a fight stance walks it to (docs/research/ai-moving.md#moving). The
/// brain clears it at the start of each update (ai::Brain::update()), so it holds only what this update's goals and
/// actions wrote.
struct BrainMove {
    float heading = 0.0F; ///< Radians, 0 facing +y.
    float speed = 0.0F;   ///< m/s; 0 stands.
    /// The brain's turn boost (`+0x0b`): its gait turn limits × (boost + 1), or ÷ (1 − boost) below 0.
    int turnBoost = 0;
    /// The point the move aims at (`+0x90`, ai::Brain::setMoveAim()): in a fight stance the feet walk along the line
    /// to it while the facing follows `heading` (docs/research/characters.md#ai-locomotion).
    anim::Vec3 aim{};
};

/// A human's per-player record (`0x00660f50 + i × 0x2c`, docs/research/ai/index.md#brain): what drives it each update.
/// A pad writes it for the player (human::Player); a brain writes the command and the stick for an AI human, as a pad
/// would (docs/research/tasks.md#humans-update). The human reads nothing else of its input.
struct PlayerRecord {
    float stickX = 0.0F;        ///< Left stick, -1 (left) to 1 (right).
    float stickY = 0.0F;        ///< Left stick, -1 (down) to 1 (up).
    anim::Vec3 cameraForward;   ///< The view the stick is turned by (a brain gives the world's axes).
    bool sprintHeld = false;    ///< L2 held.
    bool actionPressed = false; ///< Triangle pressed this update.
    combat::CommandId command = combat::command::kNone; ///< The command the human acts on (`+0x20`).
    /// A brain's climb this update toward this direction (a route's climb leg: `Climb_TryStart` toward the waypoint,
    /// docs/research/ai-moving.md#route-follow); a pad never sets it.
    std::optional<anim::Vec3> climbToward;
    /// The pad's command whether or not the human may act on it: the one matched (`+0x20`), kept under a pad lock, or
    /// else the disabled one pending (`+0x24`). What the `PadSetHandlerEx` handler is given
    /// (docs/references/bindings/input.md#padsethandlerex).
    combat::CommandId padCommand = combat::command::kNone;
    std::uint16_t buttons = 0; ///< The held buttons.
    /// A pad drives the human (per-player `+0x1b`); Humans::update() sets it every step.
    bool padDriven = false;
    /// The brain's move, which replaces the stick while set. **Coney choice**: kept beside the record, as Coney's
    /// locomotion reads only the record; the original keeps it in the brain.
    std::optional<BrainMove> move;
};

/// The record part of `input`, a pad's (so pad-driven).
/// @coney-only Coney's HumanInput written as the record; the original's record is written from the pad
/// (PlayerRecord_Update)
[[nodiscard]] PlayerRecord recordOf(const HumanInput& input);

/// What the human is doing beyond walking and standing, for the debug menus and the tests.
enum class Traversal : std::uint8_t {
    None,    ///< On the ground, walking, running or standing.
    Falling, ///< In the air after a drop.
    Jumping, ///< In the air after a jump (state flag `0x400000000`).
    Landing, ///< A jump's landing clip.
    RunStop, ///< The run stop after a run's or a sprint's skid.
    Climbing ///< A climb's clips.
};

/// A short lower-case name for `traversal` ("none", "falling", "jumping", ...), for summaries and the debug menus.
/// @coney-only names for the summaries and the debug menus
[[nodiscard]] const char* traversalName(Traversal traversal);

/// A human: position (the feet), heading, velocity, ground and air state, stamina, its fighting and its animation,
/// driven by its record. Another human fights it as a Combatant, and grabs or tackles it as a Holdable.
class Human final : public Holdable {
  public:
    /// Gravity while airborne, m/s² (1.6 g), and the fastest fall, m/s.
    static constexpr float kGravity = 15.68F;
    static constexpr float kMaxFallSpeed = 50.0F;
    /// The record `+0x08` bits under which an AI's jump is refused (`Human_BeginJump`).
    static constexpr std::uint32_t kAiJumpRefuseFlags = 0x5cfeafb;
    /// The ground snap: a ray from this high above the feet, this long, straight down.
    static constexpr float kSnapAbove = 1.0F;
    static constexpr float kSnapLength = 1.5F;
    /// Creation's snap: a ray from 1 m above, 2.5 m long, putting the feet 0.01 above the hit.
    static constexpr float kSpawnLength = 2.5F;
    static constexpr float kSpawnGap = 0.01F;
    /// A floor a falling human lands on has a normal with z above this.
    static constexpr float kFloorNormalZ = 0.65F;
    /// A velocity longer than this (times the body scale, 1) is a bug: it is zeroed.
    static constexpr float kMaxSpeed = 50.0F;
    /// A human this far below the collision mesh's lowest vertex has fallen out of the world.
    static constexpr float kOutOfWorldDepth = 20.0F;
    /// After this many airborne updates with the body unable to move as long, the human is put back on its last
    /// ground.
    static constexpr std::uint32_t kStuckUpdates = 60;
    /// A placement's drop: the ray straight down from the placed point is this long (`0x0023d440`).
    static constexpr float kPlaceDrop = 0.5F;
    /// The landing test's segment starts this far above the feet. **Coney's choice** for the body's upper point
    /// (`+0x4e8` − 0.16 × scale, `+0x4e8` not researched).
    static constexpr float kLandingTestHeight = 1.0F;
    /// A fall or a jump lands on the first update that starts with the feet this far or more below a floor its moves
    /// have passed; until then the body falls on through it (between 0.161 and 0.176 m at runtime,
    /// docs/research/characters.md#falling).
    static constexpr float kLandingDepth = 0.17F;

    /// A human playing `anims` (which must outlive it) through `slots`, bones its clips leave out taking
    /// `bindRotations` (the game's reference pose, anim::referenceRotations()), of body scale `scale` (`+0x65c`), its
    /// hits' damage from `ranges` (may be null: no damage; must outlive it). With `classDamage` (its character
    /// class's damage table, `CfgChar` `+0xb8`) the human keeps its own copy of the list with the class's damage
    /// written over it (combat::applyClassDamage(), scaled by `damagePercent`, the Warrior class byte `+0x06` of a
    /// player; 0 for none), so each human deals its own class's damage. It stands at the origin facing +y with full
    /// stamina until spawn().
    /// @coney-only Coney's construction of its members; the original's fresh state is standAfresh() (Human_Init)
    Human(const characters::AnimSet& anims, const AnimSlots& slots,
          std::span<const anim::Quat, anim::kPoseBones> bindRotations, float scale = 1.0F,
          const combat::AnimRangeList* ranges = nullptr, std::span<const std::int16_t> classDamage = {},
          int damagePercent = 0);

    /// Makes it fight as `profile` says (a player, or a human no player controls with its class's power class and
    /// health), from now and at every spawn(); its fighting starts afresh. A human is a player until told otherwise.
    /// @coney-only Coney's set-up of the fighter from its profile
    void setFighterProfile(const FighterProfile& profile);
    /// How it fights (setFighterProfile()).
    [[nodiscard]] const FighterProfile& fighterProfile() const { return m_profile; }

    /// Makes it another character where it is, as a human made with these arguments would be: it animates with
    /// `anims` (HumanAnimator::changeAnims()), fights with its own copy of `ranges` with `classDamage` scaled by
    /// `damagePercent` written over it (as the constructor takes them) and as `profile` says, its fighting afresh.
    /// What the human is to the rest of the game stays: its handle's place in the humans, its script state, its hooks
    /// and actions, its skeleton (set it again for another skeleton). Spawn it again to stand it up afresh.
    /// @coney-only the debug menus' change of character in place; the original has none
    void changeCharacter(const characters::AnimSet& anims, const combat::AnimRangeList* ranges,
                         std::span<const std::int16_t> classDamage, int damagePercent, const FighterProfile& profile);

    /// Stands a human just made (or made another character) at `position` (the feet, game axes) facing
    /// `headingDegrees` (0 faces +y), snapped to the ground of `mesh` (may be null: no snap) with a 2.5 m ray from 1 m
    /// above; 0.01 above the hit. Everything starts afresh: full health, meters and stamina, nothing in progress (a
    /// jump, a climb, a pair), the idle. Only a creation does this; moving a human that exists is place(). Human_Init
    /// makes no snap while game mode 0xb (mission complete) or 0x11 (the Rumble menus) is on top; Coney makes no human
    /// in either (its Rumble menus draw their fighters without one), so every spawn snaps.
    /// @orig 0x00218008 Human_Init (unknown)
    void spawn(const raycast::CollisionMesh* mesh, anim::Vec3 position, float headingDegrees);

    /// Moves a human that exists (a script's teleport, a scene's placement): any pair it is in is broken (its partner
    /// plays its reaction), the feet go to `position` lowered onto what a 0.5 m ray straight down from it hits (no ray
    /// with `ground` null, as for a human in a scene), facing `headingDegrees`, the stick's last angle turns to the
    /// new facing with no push, and the new point is its last ground. None of its meters, its stun or its clip is
    /// written: the health and its maximum, the stamina, the rage and power meters, a stun and the clip all stay
    /// (docs/research/teleports.md#placement). The brain's part (its front move popped) is ai::Brain::placed(), which
    /// the callers make. **Coney's choice**: a climb in progress ends (teleports.md#q-placement-rest).
    /// @orig 0x0023d440 Human_SetTransform (unknown)
    void place(const raycast::CollisionMesh* ground, anim::Vec3 position, float headingDegrees);

    /// The motion reset `TeleportToFlag` makes after its placement: the move velocity across the ground is zero
    /// (`Human_SetMoveVelocity` with the zero vector) and the attack's steer is cleared; the clip, the vertical speed
    /// and the meters stay (docs/research/teleports.md#motion-reset).
    /// @orig 0x0023f158 Human_ResetMotion (unknown)
    void resetMotion();

    /// Stands the human as after a walk, where it is: idle, out of any stance, attack, stun, pair or traversal, its
    /// health and its maximum, stamina and meters kept. For Coney's own placements that stand for a walk (the debug
    /// menus' teleport, `--start`) and for the hand-over's new player 1 (human::makePlayerOf()).
    /// @coney-only the walk stand-in of the test aids and the hand-over's stand-in; the original has no such call
    void settle();

    /// Puts a human that fell out of the world back at `position` facing `headingDegrees`: standing, still, snapped
    /// to `ground` as a creation snaps (a 2.5 m ray from 1 m above, 0.01 above the hit, so a start point at or a little
    /// under the surface is found), its health and meters kept. The original has no put-back: the fall fails the
    /// mission (docs/research/teleports.md#out-of-world).
    /// @coney-only a fallen player put back, where the original fails the mission
    void putBack(const raycast::CollisionMesh* ground, anim::Vec3 position, float headingDegrees);

    /// One update of 1/30 s for this human alone: `input` written into its record, then animate(), updateState() and
    /// updateActions() in turn. human::Humans runs the same passes across every human in the original's order.
    /// @coney-only one human updated alone (the tests, a scene of one); the original updates every human together
    /// (Humans_Update)
    void step(const HumanInput& input, const raycast::CollisionMesh* mesh);

    /// The per-player record the next update reads; a pad or a brain writes it before the update.
    [[nodiscard]] PlayerRecord& record() { return m_record; }
    [[nodiscard]] const PlayerRecord& record() const { return m_record; }

    /// The update's first pass, the animation step: the gait the last update left is kept for the dispatcher's gait
    /// tests, then every task advances (the clips' events moving the record's
    /// `+0x08`), and a climb follows its clips. Nothing for a human out of the world.
    /// @orig 0x0023bd78 Human_StepAnimation (unknown)
    void animate(const raycast::CollisionMesh* mesh);
    /// The second pass, the state update: the record's stick turned by its camera; the locomotion (through the
    /// locomotion gate, human/locomotion_gate.h), the jump's air control or the climb's own motion, with the clip's
    /// root motion added; gravity while airborne; the move against `mesh`'s walls with the ground snap, or the fall
    /// and landing.
    /// @orig 0x00240e38 Human_PlayerLocomotion (unknown)
    /// @orig 0x0023fea8 Human_StateUpdate (unknown)
    void updateState(const raycast::CollisionMesh* mesh);
    /// The third pass, the actions: stamina and the sprint, combat's dispatcher from the record's command (against
    /// `targets`, of which the fight stance never takes those setCalm() gave), triangle's climb or jump, the lean and
    /// the animation state.
    void updateActions(std::span<Combatant* const> targets, const raycast::CollisionMesh* mesh);
    /// Of the next updateActions()' targets, those the fight stance never takes (no threat, or a pad player); it must
    /// outlive that call, which uses it once.
    void setCalm(std::span<Combatant* const> calm) { m_calm = calm; }

    /// The pose to draw now: its clips over the default pose, which is the face clips' (human/face.h) while the face is
    /// shown (faceShown()), else the bind rotations it was made with. Ports only the face's part of the original
    /// (the default pose); its head look, torso twist and bone-matrix flag wait for #963.
    /// @orig 0x00176d60 CharacterInstance_Sample (unknown)
    [[nodiscard]] anim::Pose pose() const;

    /// Its face's talk state: the mood its brain set and whether a line it says is playing.
    [[nodiscard]] const FaceState& faceState() const { return m_faceState; }
    /// Its face clips.
    [[nodiscard]] const FaceLayer& faceLayer() const { return m_face; }
    /// Sets the mood its face shows (a brain's, every fifth update: ai::faceMoodOf()).
    void setFaceMood(FaceMood mood) { m_faceState.setMood(mood); }
    /// Whether a line it says is playing (the sound's, each step): the start turns its face to the mood's speaking
    /// clips, the end back (`Human_UpdateSpeech` and `Human_StopSpeech` begin and end the talk pose with the stream).
    /// @coney-only glue: the original's speech update and stop call the begin and end themselves (`+0x170` marks
    /// a begun line); Coney's speech lives in the sound, which hands its state over each step
    void setSpeaking(bool speaking);
    /// The rules its face follows (whose face shows, the face clips' random stream), human::Humans's; not owned, null
    /// for none: then its face always shows and plays the first clip of a state every time.
    void setFaceRules(FaceRules* rules) { m_faceRules = rules; }
    /// Whether its face clips show now: the rules say so and it is neither down nor dead.
    /// @orig 0x00223440 Human_UsesQueuedPose (unknown)
    [[nodiscard]] bool faceShown() const;

    /// The skeleton its strike shapes are posed on: its character's (not owned; it must outlive the human). Null (the
    /// default): it has no strike shapes, and its attacks land at their hit update (combat::attackHitUpdate()).
    void setSkeleton(const anim::Skeleton* skeleton) { m_skeleton = skeleton; }
    /// Which of its strike shapes its clips' events have switched on (human/strike_shapes.h), and what they struck.
    [[nodiscard]] const StrikeShapes& strikeShapes() const { return m_strikes; }
    [[nodiscard]] StrikeShapes& strikeShapes() { return m_strikes; }
    /// The anim id whose clip's events last switched a strike shape: the shapes on belong to it.
    [[nodiscard]] std::uint32_t strikeAnim() const { return m_strikeAnim; }
    /// Its strike shapes posed in the world now: the ones switched on (and the capsule while its strike flag is on),
    /// or with `targets` the spine and the head, which a strike is tested against (flag `0x4`). Empty without a
    /// skeleton. The bones' positions carry the body scale (docs/research/combat/index.md#moving-strikes).
    /// @orig 0x0023bde8 Human_GetBoneTransform (unknown)
    [[nodiscard]] std::vector<PosedShape> posedStrikeShapes(bool targets) const;
    /// The capsule a capsule strike (clip flag `0x10000`) is tested against: upright from the feet to the head bone's
    /// height + 0.2, never under 0.2, of radius 0.35, all times the scale
    /// (docs/research/combat/index.md#capsule-strike).
    /// **Coney choice**: the standing height in every state.
    /// @orig 0x00343518 PhysicsCapsule_Pose (unknown)
    [[nodiscard]] PosedShape struckCapsule() const;
    /// The body point (`+0x4e0`): the hips' (pose bone 2) position in the body's own frame, z up and facing +y, not
    /// turned by the heading, times the scale; none without a skeleton (docs/research/camera.md#body-point).
    /// @orig 0x0023bde8 Human_GetBoneTransform (unknown)
    [[nodiscard]] std::optional<anim::Vec3> bodyPoint() const;
    /// What a human's switched-on strike shapes meet beyond the humans (the level's objects): `shapes`, posed now.
    /// Humans::setStrikeContact() gives it.
    using StrikeContact = std::function<void(Human& human, std::span<const PosedShape> shapes)>;
    /// The strike test, after every human's move (human::Humans): while any strike shape is on, its posed shapes
    /// meet each of `victims` (the humans it fights) whose spine or head they overlap (its capsule for a clip with flag
    /// `0x10000`, struckCapsule()), each once while the shapes stay
    /// on, swept from where each shape was posed the update before (sweptShapesMeet()), and the free attack playing
    /// (Fighter::strikesWithShapes()) hits it there (Fighter::strikeContact()); then
    /// `contact`
    /// (may be null) hears the shapes. **Coney choices**: the capsule is taken as the 2 m upright capsule of its
    /// radius; the struck victims are the humans it fights, not every body near it.
    /// @orig 0x0033f110 Human_TestStrikes (unknown)
    void testStrikes(std::span<Human* const> victims, const StrikeContact* contact);

    [[nodiscard]] anim::Vec3 position() const override { return m_position; }
    /// Radians, 0 facing +y, anticlockwise from above.
    [[nodiscard]] float heading() const override { return m_heading; }
    /// How it lies, for an attacker: grounded while knocked down or out of health, held while in a grab, else
    /// standing.
    /// @coney-only Coney's summary of the down and held states for an attacker
    [[nodiscard]] TargetState state() const override;
    /// Its health (record `+0x144`).
    [[nodiscard]] const combat::Health& health() const override { return m_fighter.health(); }
    [[nodiscard]] anim::Vec3 velocity() const override { return m_velocity; }
    /// The horizontal speed.
    /// @coney-only a read of the velocity
    [[nodiscard]] float speed() const;
    /// The gait of the whole velocity's length (`+0x1a8`, written with the velocity: a jump's vertical speed counts).
    /// @coney-only a read of the velocity as a gait
    [[nodiscard]] Gait gait() const;
    /// Whether it is airborne (off the ground).
    [[nodiscard]] bool airborne() const { return m_airborne; }
    /// The human fell more than kOutOfWorldDepth below the mesh; it stays put until spawned again.
    [[nodiscard]] bool outOfWorld() const { return m_outOfWorld; }
    /// Where it last stood on the ground (a creation's or a placement's point, or its feet on the last update it
    /// stood): where a human stuck in the air for two seconds lands.
    [[nodiscard]] anim::Vec3 lastGround() const { return m_lastGround; }
    /// With a skeleton and in the world, its spine and head shapes are posed each update for the strike test.
    [[nodiscard]] bool struckByShapes() const override { return m_skeleton != nullptr && !m_outOfWorld; }
    /// The head (bone 6) as posed now, in the world; the position without a skeleton.
    /// @orig 0x0023cd30 Human_UpdateLedSlots (unknown)
    [[nodiscard]] anim::Vec3 ledPoint() const override;
    /// The ground normal from the last snap.
    [[nodiscard]] anim::Vec3 groundNormal() const { return m_groundNormal; }
    /// The flags of the collision triangle the last ground snap stood on (`flags & 0xfff`); 0 when it found none.
    [[nodiscard]] std::uint16_t groundFlags() const { return m_groundFlags; }
    /// Whether it stands on shadow ground: the ground snap's triangle has raycast::kTriangleShadow
    /// (docs/research/stealth.md#shadow-ground).
    [[nodiscard]] bool onShadowGround() const { return (m_groundFlags & raycast::kTriangleShadow) != 0; }

    // The hidden state (state `0x200000`, docs/research/stealth.md#hidden). The ground rules that start and end it
    // (who may hide, when) are the caller's (ai/hiding.h); these are what entering and leaving do to the human.

    /// Whether it is hidden (state `0x200000`), the grace after leaving the shadow included.
    [[nodiscard]] bool hidden() const { return m_hidden; }
    /// Enters the hidden state: nothing when already hidden or sprinting. Its target no longer locks
    /// (Fighter::setLockBlocked()), so it leaves the fight stance, and its locomotion takes the hidden move style
    /// (HumanAnimator::setStealthStyle()); any grace is cleared.
    /// @orig 0x0022ff88 Human_EnterShadow (unknown)
    void enterHiding();
    /// Leaves the hidden state, as stepping off the shadow does: when it is not running and has a target, the state
    /// is kept for a 4 s grace (once: a grace already running keeps its end); otherwise clearHiding(). Nothing when not
    /// hidden.
    /// @orig 0x002300c0 Human_LeaveShadow (unknown)
    void leaveHiding();
    /// Ends the hidden state at once: the move style comes off, the target may lock again and the grace is cleared.
    /// @orig 0x00230140 Human_ClearHiddenState (unknown)
    void clearHiding();
    /// The grace's length after leaving the shadow with a target, ms.
    static constexpr std::uint64_t kHideGraceMs = 4000;
    /// The vertical speed at the last landing (negative), 0 before any.
    [[nodiscard]] float lastLandingSpeed() const { return m_lastLandingSpeed; }
    /// How many times it has landed (a fall or a jump), for a move that must notice a landing.
    [[nodiscard]] std::uint32_t landings() const { return m_landings; }
    /// The last stick intent, after the camera's turn.
    [[nodiscard]] const StickIntent& intent() const { return m_intent; }
    /// Its clips.
    [[nodiscard]] const HumanAnimator& animator() const { return m_animator; }
    /// `HuUseAnim`'s idle replacement, `name` loaded as `clip` (null for none: the own idle);
    /// HumanAnimator::setIdleClip(). The play mode applies ScriptState's override for the idle (`0x184`) through it.
    /// @orig 0x00221aa0 Human_SetAnimOverride (unknown)
    void setIdleClip(std::string_view name, const anim::AnimClip* clip) {
        m_idleClipName = std::string(name);
        m_animator.setIdleClip(clip);
    }
    /// `Human_PushOverlayClip`: `clip` played as anim `animId` over what plays, at the rate its Anim Range List flags
    /// give it (characters::AnimSet::rate(): 0.75 for a clip with none), at full weight from its first update (the
    /// original passes 0 for both of the task's fades), with `held` set while it plays
    /// (docs/research/formats/animation.md#playback-rate). **Coney stand-ins**: the original ends a clip on the update
    /// its frame would reach its count (ceil((F - 0.5) / rate), 53 for the flash's 40 frames, as measured), Coney's
    /// clip tasks when the time reaches the length (54), so this clip hands over a step early, as clip 34 does, until
    /// the clip tasks take the original's rule (formats/animation.md#clip-end); and the original pushes a layer over
    /// the stack, so what played before (a held block, say) shows again when the clip ends, where Coney's clip hands
    /// over to the idle and the controller chooses again (what shows on the update after 665 is not measured).
    /// @orig 0x0025ade8 Human_PushOverlayClip (unknown)
    /// @orig 0x00106240 AnimTask5_Construct (unknown)
    void pushOverlayClip(const anim::AnimClip& clip, std::uint32_t animId, HeldFlags held) {
        m_animator.playScripted(clip, animId, m_animator.anims().rate(animId), 0.0F, held, true);
    }
    /// A scripted clip played from outside the dispatcher (HumanAnimator::playScripted()).
    void playScripted(const anim::AnimClip& clip, std::uint32_t animId, float rate, float fade, HeldFlags held) {
        m_animator.playScripted(clip, animId, rate, fade, held);
    }
    /// Ends the scripted clip of anim `animId` with the idle while it still plays holding any of `held` (an AI's
    /// fidget or taunt cut short, `Human_StopFidget`); nothing otherwise.
    /// @orig 0x00231b60 Human_StopFidget (unknown)
    void stopScripted(std::uint32_t animId, std::uint32_t held) {
        if ((m_animator.flags() & held) != 0 && m_animator.animId() == animId) {
            m_animator.stopToIdle();
        }
    }
    /// A workout's clips (HumanAnimator::playWorkout()), its loop blend once playing, and its exit
    /// (HumanAnimator::endWorkout()).
    void playWorkout(const anim::AnimClip& enter, const std::array<const anim::AnimClip*, 3>& loops) {
        m_animator.playWorkout(enter, loops);
    }
    /// The workout's loop while it plays (HumanAnimator::workoutLoop()); null otherwise.
    [[nodiscard]] anim::GaitBlendTask* workoutLoop() const { return m_animator.workoutLoop(); }
    /// Ends the workout with its `exit` clip (HumanAnimator::endWorkout()).
    void endWorkoutClips(const anim::AnimClip* exit) { m_animator.endWorkout(exit); }
    /// **Coney's own**: the debug menus' Animation player plays `clip` on the human (HumanAnimator::playDebugClip(),
    /// docs/guides/debug-menu.md#animation-player); the original has nothing like it.
    void playDebugClip(const anim::AnimClip& clip, std::uint32_t animId, float rate, bool loop, bool inPlace) {
        m_animator.playDebugClip(clip, animId, rate, loop, inPlace);
    }
    /// **Coney's own**: ends the Animation player's clip (HumanAnimator::stopDebugClip()).
    void stopDebugClip() { m_animator.stopDebugClip(); }
    /// The name of the idle replacement applied (empty for none).
    [[nodiscard]] const std::string& idleClipName() const { return m_idleClipName; }
    /// `HuUseAnyAnim`'s replacement for anim `id` other than the idle, `name` loaded as `clip` (null: the own clip
    /// plays); an empty `name` takes it out. HumanAnimator::setOverride().
    /// @orig 0x00221aa0 Human_SetAnimOverride (unknown)
    void setOverrideClip(std::uint32_t id, std::string_view name, const anim::AnimClip* clip) {
        if (name.empty()) {
            m_overrideNames.erase(id);
        } else {
            m_overrideNames[id] = std::string(name);
        }
        m_animator.setOverride(id, clip);
    }
    /// The replacements applied by setOverrideClip(), by anim id.
    [[nodiscard]] const std::map<std::uint32_t, std::string>& overrideClipNames() const { return m_overrideNames; }
    /// Called with each anim id the human starts playing, for the scripts' animation callbacks
    /// (docs/research/characters.md#anim-callbacks); empty for none.
    void setAnimStartHook(std::function<void(std::uint32_t animId)> hook) { m_animator.setStartHook(std::move(hook)); }
    /// The sounds its updates asked for since the last call, oldest first: its clips' animation sounds (clip event
    /// 11) and the hit sounds of the hits it took (docs/research/sound-events.md). Kept only once asked for
    /// (reportSounds()), so a human nobody listens to keeps none.
    /// @coney-only the hand-over of its sounds to Coney's audio
    [[nodiscard]] std::vector<HumanSound> takeSounds();
    /// Starts (or stops) keeping its sounds for takeSounds(). Asking for what it does already changes nothing: the
    /// sounds it holds stay.
    /// @coney-only the switch of Coney's sound hand-over
    void reportSounds(bool on);
    /// Adds a sound the level found for this human, such as his strike on the level, an object or a car. It is kept
    /// for takeSounds() only while he reports sounds.
    /// @coney-only the hand-over of the level's sounds to Coney's audio
    void reportSound(const HumanSound& sound);
    /// The material of the ground under the feet (`+0x1d8`): the triangle the last ground snap hit, `CONCRETE` (5)
    /// before any. **Coney's reading**: the snap's triangle (docs/research/sound.md#anim-sounds names `+0x1d8` the
    /// ground under the human).
    [[nodiscard]] std::uint8_t groundMaterial() const { return m_groundMaterial; }
    /// Whether the last ground snap stood him on a triangle with flag bit 5 (`0x20`), the covered ground that decides
    /// which ambient emitters a player hears (`+0x5b7`, docs/research/sound-events.md#covered).
    /// @orig 0x002195e0 Human_SetCoverFlag5 (unknown)
    [[nodiscard]] bool onCoveredGround() const { return m_coveredGround; }
    /// The speeds its clips give the locomotion (HumanAnimator::speeds()).
    [[nodiscard]] const Speeds& speeds() const { return m_animator.speeds(); }
    /// Stamina (record `+0x14a`).
    [[nodiscard]] const Stamina& stamina() const { return m_stamina; }
    /// Fills the stamina to its maximum (a human made a player, human::takeMetersOf()).
    void fillStamina() { m_stamina.fill(); }
    /// Whether a sprint is asked for (state flag `0x1000000`): L2 held with stamina left.
    [[nodiscard]] bool sprinting() const { return m_sprinting; }
    /// The body's lean, radians (`+0x29c`), positive leaning left.
    [[nodiscard]] float lean() const { return m_lean; }
    /// What the human is doing beyond walking.
    /// @coney-only a summary of the human's state for the debug menus and the tests
    [[nodiscard]] Traversal traversal() const;
    /// What the locomotion gate reads of the human now: the record's `+0x08` and state code, airborne, held.
    /// @coney-only what Coney's locomotion gate reads, gathered from the human
    [[nodiscard]] GateInput gateInput() const;
    /// Whether the stick does not move the human now: combat's states hold the body (Fighter::holdsMovement()), it is
    /// busy (stickBusy()), the stick's velocity is gated (stickVelocityGated()) or it is arrested.
    /// @coney-only Coney's summary of the gates for the stick
    [[nodiscard]] bool stickHeld() const;
    /// The climb in progress, if any.
    [[nodiscard]] const std::optional<ClimbProbe>& climb() const { return m_climbProbe; }
    /// Climbing over: the climb's middle clip (for the tall and the short fence 438 and 444 standing, 441 and 447
    /// running), while record `+0x08` holds bit `0x40` (fences do not block the body); not its first or last clip.
    /// @orig 0x00223c10 Human_HasHeld40 (unknown)
    [[nodiscard]] bool climbingOver() const { return m_climbRun.has_value() && m_climbRun->over; }
    /// `Human_IsDownOrDead`'s states `0x180050000` as Coney models them: no health left (dead, dying), knocked out or
    /// wounded (`0x10000`). Knocked down alive (`0x80000`) and cuffed (`0x20000`) are not among them. The narrow
    /// reading: ai::downOrDead() (src/ai/gangs.h) is Coney's wider one, which also counts a human knocked down alive.
    /// @orig 0x00227e60 Human_IsDownOrDead (unknown)
    [[nodiscard]] bool downOrDead() const {
        return m_fighter.health().depleted() || m_script.knockedOut || m_script.wounded;
    }
    /// Knocked out, state `0x40000`: its health run out (`Human_OnHealthOut` 0x002674c0 knocks a player, flag `0x4`,
    /// out through `Human_KnockOut` 0x00230328) or a script's `HuSetConscious(false)`
    /// (docs/research/combat-reactions.md#knocked-out-bit).
    /// @orig 0x00227dd8 Human_IsKnockedOut (unknown)
    /// Stand-in: Coney keeps no state word, so health run out counts as knocked out for every human; in the original
    /// one without flag `0x4` may be wounded (flag `0x8`) or made dead by a kind-29 hit instead, without the bit, and a
    /// human with no record (`+0xd4`) counts as knocked out (every Coney human has one).
    [[nodiscard]] bool knockedOut() const { return m_fighter.health().depleted() || m_script.knockedOut; }
    /// Whether `Player_UseItemCommand` refuses the flash's press outright: down or dead, cuffed, airborne
    /// (`0x1c00000000`) or climbing over. A knockdown, a stun, a hit reaction or a climb's first or last clip is none
    /// of these: there the flash is used at once (docs/research/combat-rage.md#flash-mid-move).
    /// @orig 0x002843f8 Player_UseItemCommand (unknown)
    [[nodiscard]] bool refusesFlash() const {
        return downOrDead() || m_script.arrested || airborne() || climbingOver();
    }
    /// The body's scale (`+0x65c`): the mesh's, and the collision's too unless collisionFollowsMesh() is off.
    [[nodiscard]] float scale() const { return m_scale; }
    [[nodiscard]] float bodyScale() const override { return m_scale; }
    /// The scale the collision is sized by: scale(), or the configured one while collisionFollowsMesh() is off.
    [[nodiscard]] float collisionScale() const { return m_collisionFixed.value_or(m_scale); }
    /// The scale the human was made with (a script's `HuSetScale` is an edit of it, not its configuration).
    /// @coney-only debug tooling: the Entity editor's reset and its *Scale collision with mesh*
    [[nodiscard]] float configuredScale() const { return m_configuredScale; }
    /// Whether the collision (the walking and air spheres, the upright capsule and the strike shapes) scales with the
    /// mesh: on at first, so a giant collides at its drawn size. The struck capsule (struckCapsule(), a port) keeps
    /// following scale() either way.
    [[nodiscard]] bool collisionFollowsMesh() const { return !m_collisionFixed.has_value(); }
    /// Makes the collision follow the mesh scale (`follow`), or stay at the configured scale whatever setScale() says.
    /// @coney-only debug tooling: the Entity editor's *Scale collision with mesh*
    void setCollisionFollowsMesh(bool follow) {
        m_collisionFixed = follow ? std::nullopt : std::optional<float>(m_configuredScale);
    }
    /// The radius of its collision capsule (the shape's `+0x40`, 0.35 m, × its scale): human::walkingRadius().
    [[nodiscard]] float capsuleRadius() const { return walkingRadius(collisionScale()); }
    /// The walking sphere the wall sweep moves (docs/research/characters.md#walls, step 1): a player's
    /// playerWalkingRadius(), an AI human's aiWalkingRadius(), of its scale now.
    /// @coney-only the body's size as Coney's sweep takes it (the original keeps it on its physics body)
    [[nodiscard]] float walkingSphereRadius() const;
    /// How far above the feet the walking sphere's centre is: its radius plus BodyTuning::footGap.
    /// @coney-only the body's size as Coney's sweep takes it (the original keeps it on its physics body)
    [[nodiscard]] float walkingSphereCentreHeight() const;
    /// The sphere the airborne body is pushed out of walls with: BodyTuning::airRadius × the scale, its centre its
    /// radius plus the foot gap above the feet (docs/research/characters.md#walls).
    /// @coney-only the body's size as Coney's push-out takes it (the original keeps it on its physics body)
    [[nodiscard]] float airSphereRadius() const;
    /// Its capsule as another human's sweep meets it, and as the way-clear test (human::wayClear()) takes it.
    /// @coney-only the capsule as Coney's sweeps and way-clear test take it
    [[nodiscard]] UprightCapsule uprightCapsule() const;
    /// The normals of what the last walking sweep slid along (walls, other humans' capsules, world objects' bodies),
    /// each pointing out of what was met; empty after a move that met nothing. The velocity slides along the same.
    [[nodiscard]] std::span<const anim::Vec3> contacts() const { return m_contacts; }
    /// Updates in a row the walking move was blocked outright (the body stayed), or the airborne body could not move.
    [[nodiscard]] std::uint32_t blockedUpdates() const { return m_blockedUpdates; }
    /// Updates spent in the air since the last landing.
    [[nodiscard]] std::uint32_t airborneUpdates() const { return m_airborneUpdates; }
    /// `HuSetScale`: the body's scale, not range-checked. Coney's body (its capsule, its walls' radius, its root
    /// motion) is sized from the scale at each use, so the body follows at once.
    /// @orig 0x0023b0e0 Human_SetScale (unknown)
    void setScale(float scale) { m_scale = scale; }

    // Coney's debug tooling: what the Entity editor changes on a human (docs/guides/debug-menu.md#entity-editor).

    /// How much bigger the mesh is drawn than configured, 1 as made: only the Entity editor's scale changes it. The
    /// drawn body only: the physics and the collision are scale() and collisionScale().
    /// @coney-only debug tooling: the Entity editor's scale
    void setMeshScale(float scale) { m_meshScale = scale; }
    /// The mesh's drawn scale setMeshScale() set.
    /// @coney-only debug tooling: the Entity editor's scale
    [[nodiscard]] float meshScale() const { return m_meshScale; }
    /// A multiplier on every gait speed of this human, on top of the player's own Movement tunables; 1 is as
    /// configured. It lands at once and reads through speeds().
    /// @coney-only debug tooling: the Entity editor's speed scale
    void setSpeedScale(float scale);
    /// The gait speeds' multiplier setSpeedScale() set.
    /// @coney-only debug tooling: the Entity editor's speed scale
    [[nodiscard]] float speedScale() const { return m_speedScale; }
    /// A multiplier on how fast every clip plays (and so how far its root motion carries); 1 is as configured, 0 holds
    /// the clips.
    /// @coney-only debug tooling: the Entity editor's animation rate
    void setAnimRate(float rate) { m_animRate = rate < 0.0F ? 0.0F : rate; }
    /// The clips' play-rate multiplier setAnimRate() set.
    /// @coney-only debug tooling: the Entity editor's animation rate
    [[nodiscard]] float animRate() const { return m_animRate; }
    /// The health set to `percent` of its maximum (rounded; at least 1 point, so a kill is its own edit; above 100 the
    /// maximum).
    /// @coney-only debug tooling: the Entity editor
    void setHealthShare(float percent);
    /// The stamina set to `percent` of its maximum (rounded).
    /// @coney-only debug tooling: the Entity editor
    void setStaminaShare(float percent);
    /// The power meter set to `percent` of its maximum (rounded).
    /// @coney-only debug tooling: the Entity editor
    void setPowerShare(float percent);
    /// The rage meter set to `percent` of its maximum (truncated, as `HuSetRageFrac` does).
    /// @coney-only debug tooling: the Entity editor
    void setRageShare(float percent);
    /// Empties the health; the human's own update turns that into a death, as damage that takes the last point does.
    /// @coney-only debug tooling: the Entity editor's Kill
    void kill() { m_fighter.health().set(0); }
    /// The fighting: combat's state, meters and what it last did.
    [[nodiscard]] const Fighter& fighter() const { return m_fighter; }
    [[nodiscard]] Fighter& fighter() { return m_fighter; }
    /// What the run log calls this human (`human 12 warr_cl`), kept for its fighter across a new fighting profile or a
    /// spawn (Fighter::setLogName()).
    /// @coney-only a name for Coney's run log (docs/guides/run-log.md)
    void setLogName(std::string name) {
        m_logName = std::move(name);
        m_fighter.setLogName(m_logName);
    }
    /// The Anim Range List its hits take their damage from (its own copy when made with a class damage table); null
    /// for none.
    [[nodiscard]] const combat::AnimRangeList* ranges() const { return m_ranges; }
    /// `HuApplyDamageModifier(human, factor)`: its attack damages become its class's values × `factor`, rounded (then
    /// a player's Warrior class percentage, as when it was made); each call starts from the class values, and an entry
    /// that rounds to 0 keeps its damage. Nothing for a human made without a class damage table.
    /// @orig 0x00229b90 Human_ScaleAttackDamages (unknown)
    void applyDamageModifier(float factor);

    // Being attacked: the entry points another human and the tests use. Each is acted on at the human's next step
    // (docs/research/combat-reactions.md#being-hit-runtime).

    /// A hit on the human (Fighter::takeHit()): the update keeps its largest.
    void takeHit(const IncomingHit& hit) { m_fighter.takeHit(hit); }
    /// Combatant's name for takeHit().
    void hit(const IncomingHit& hit) override { m_fighter.takeHit(hit); }
    /// An attacker's clip warns the human of its hit (Fighter::warn()): a blocking human ducks or blocks early.
    void warn(const AttackNotice& notice) override { m_fighter.warn(notice); }
    /// Keeps where an attack started near it (event `0x10`) for its brain, which counts them (brain `+0x200`).
    void announceAttack(anim::Vec3 attacker) override { m_announced.push_back(attacker); }
    /// Where the attacks announced since the last call started, and forgets them (its brain takes them each update).
    /// @coney-only Coney's hand-off to the brain; the original sends the brain its event at once
    [[nodiscard]] std::vector<anim::Vec3> takeAttackAnnouncements() { return std::exchange(m_announced, {}); }
    /// The slow-motion event (type `0x2e` on, `0x2f` off) the clip playing passed in the last update, if any; the
    /// later of the two when both did (docs/research/camera.md#slow-motion).
    [[nodiscard]] std::optional<std::uint16_t> slowMotionEvent() const { return m_slowMotionEvent; }
    /// Knocked out (its health run out, or `HuSetConscious(false)`): its player's slow motion goes off, as a clip's off
    /// event does; takeSlowMotionEnd() hands it to the player (docs/research/slow-motion.md#off).
    /// @orig 0x00230328 Human_KnockOut (unknown)
    void endSlowMotion() { m_slowMotionEnd = true; }
    /// Whether endSlowMotion() was called since the last call.
    [[nodiscard]] bool takeSlowMotionEnd() { return std::exchange(m_slowMotionEnd, false); }
    /// Whether it left normal mode (setNormalMode(), resetForJoin()) since the last call, which turns every player's
    /// slow motion off (camera::SlowMotion::reset(), docs/research/slow-motion.md#off).
    [[nodiscard]] bool takeSlowMotionReset() { return std::exchange(m_slowMotionReset, false); }
    /// The anim whose action event (type `0x41`, which `Anim_FireFrameEvents` 0x00101dd8 sends as message `0xc1`) the
    /// clip playing passed in the last update, if any: for 665 `SPECIAL_FLASH` the flash is used, for 666 the door
    /// ahead opens and for 668 the dealer's goal is served (docs/research/combat-rage.md#flash).
    [[nodiscard]] std::optional<std::uint32_t> actionEvent() const { return m_actionEvent; }
    /// Sets the characters' step its next updates advance by: kStepSeconds, or less in slow motion
    /// (docs/research/camera.md#slow-motion; Humans::update() sets it for every human).
    void setStepSeconds(float seconds) { m_stepSeconds = seconds; }
    /// The characters' step its updates advance by.
    [[nodiscard]] float stepSeconds() const { return m_stepSeconds; }
    /// Whether a target search may pick it: not with flag::kNoTarget, nor when its gang was made untargetable.
    [[nodiscard]] bool targetable() const override {
        return m_script.targetable && !m_fighter.hasFlag(flag::kNoTarget);
    }

    // Held in another human's grab or tackle (Holdable, human_held.cpp): the grabber plays its clips and, attached,
    // places it each update; meanwhile its own update neither moves it by the stick or its brain nor lets it act.

    /// The human itself, unless nobody may grab it (flag::kUngrabbable, `HuSetUngrabbable`).
    [[nodiscard]] Holdable* holdable() override { return hasFlag(flag::kUngrabbable) ? nullptr : this; }
    void play(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state,
              TargetState targetState) override;
    void playPaired(std::span<const std::uint32_t> clips, const characters::AnimSet& attacker, std::uint32_t loop,
                    AnimState state, TargetState targetState) override;
    void announceFollowOn() override { m_animator.announceFollowOn(); }
    void setAttached(bool attached) override { m_fighter.setHoldAttached(attached); }
    [[nodiscard]] bool attached() const override { return m_fighter.holdAttached(); }
    void keepHold() override { m_fighter.keepHold(); }
    [[nodiscard]] std::optional<TargetState> takeBrokenHold() override { return m_fighter.takeBrokenHold(); }
    [[nodiscard]] std::optional<CounterPress> takeCounterPress() override { return m_fighter.takeCounterPress(); }
    void heldInGrab(const combat::GrabberState& grabber, bool fromRear) override {
        m_fighter.heldInGrab(grabber, fromRear);
    }
    [[nodiscard]] GrabbedReport takeGrabbedReport() override { return m_fighter.takeHeldReport(); }
    void holdSide(bool rearGrab) override { m_fighter.setRearHeld(rearGrab); }
    [[nodiscard]] bool heldFromRear() const override { return m_fighter.heldFromRear(); }
    [[nodiscard]] std::uint32_t heldFlags() const override { return m_animator.flags(); }
    /// The tandem's victim side (Fighter::startTandemVictim()); cuffed is its arrest (state `0x20000`), which
    /// `Human_IsDamageDefeating` triples the damage for.
    bool startTandem(const TandemStart& start) override {
        return m_fighter.startTandemVictim(start, m_animator, m_script.arrested, m_script.knockedOut);
    }
    [[nodiscard]] std::optional<int> takeTandemRequest() override { return m_fighter.takeTandemRequest(); }
    /// Moves it there and stops it (its velocity goes): the attached hold's placement of the held human at the
    /// transform the grabber gives (fighter_grab.cpp), not a script's placement (place() above). It ports the placing
    /// part of `Human_MoveAttached` only. **Coney stand-in**: the original also sweeps the held body toward the point
    /// against the others (`PhysicsBody_Sweep`, up to three tries) and hands the holder's velocity on; Coney's held
    /// human has its movement held and moves with no velocity of its own, and what the sweep changes (a grab against a
    /// wall) is not measured (docs/research/combat-grabs.md#grab-posing). The locked camera's kept-in-view push
    /// (gameplay_mode.cpp) borrows it to move a human: `LockedCam_KeepHumansInView` (`0x00135ca8`) sets the human's
    /// transform through its slot `+0x6c`, which for a human is `Human_SetTransform`, with the rotation kept. **Coney
    /// stand-in** there: the mover does none of that placement's pair break, 0.5 m drop or front-move pop (place()
    /// above), and the stop of the velocity is Coney's.
    /// @orig 0x00244e78 Human_MoveAttached (unknown)
    void place(anim::Vec3 position, float headingRadians) override;
    /// Thrown: its fighter's steer carries it (Fighter::carryOver()), applied in each state update as an attack's is.
    void carryOver(anim::Vec3 goal, float headingRadians, float seconds) override {
        m_fighter.carryOver(m_position, m_heading, goal, headingRadians, seconds);
    }
    /// Flies the human while `flight` is set (the debug menu's noclip, docs/guides/debug-menu.md#noclip; Coney's own,
    /// with no counterpart in the original): each state update moves it by the record's stick, turned by the camera
    /// with the game's dead zone, at the flight's speed, and by its climb, with no collision, gravity, falling or
    /// landing, facing where it flies across the ground; its clips do not move it. Starting a flight lets go of a pair,
    /// a climb or a jump and stands it in the idle; ending one (nullopt) leaves it standing where it is, so the next
    /// update's ground test drops it from there with the normal physics. Set it each update to change the speeds.
    void setFlight(std::optional<Flight> flight);
    /// Whether it flies (setFlight()).
    [[nodiscard]] bool flying() const { return m_flight.has_value(); }
    /// An AI's jump (`Human_BeginJump` with argument 1, then `Human_LaunchJump`,
    /// docs/research/ai-moving.md#route-jump): from any gait, standing too, launched at `velocity` with the jump's
    /// clip. Refused (false) in the air, while climbing, or while the record `+0x08` holds any of kAiJumpRefuseFlags.
    /// @orig 0x0023db48 Human_BeginJump (unknown)
    /// @orig 0x002217f0 Human_LaunchJump (unknown)
    bool launchJump(anim::Vec3 velocity);
    void face(anim::Vec3 point) override;
    [[nodiscard]] const characters::AnimSet& anims() const override { return m_animator.anims(); }

    // The scripts' hold on the human (docs/references/bindings/character.md): its flags and state, and what they do to
    // it. Each acts at once; the flags and the state keep across spawn().

    /// The flag word (human `+0xe0`, human/human_flags.h), kept by its fighter.
    [[nodiscard]] std::uint64_t flags() const override { return m_fighter.flags(); }
    /// Its character class (human `+0xcc`); its brain gives it (ai::Brain::setCharacterClass()), -1 until then.
    [[nodiscard]] int characterClass() const override { return m_characterClass; }
    /// Sets its character class (characterClass()).
    void setCharacterClass(int characterClass) { m_characterClass = characterClass; }
    /// Its class's category is 13 (FighterProfile::bossClass).
    [[nodiscard]] bool bossClass() const override { return m_fighter.bossClass(); }
    /// Whether its fighter's flags have `bit` (Fighter::hasFlag()).
    [[nodiscard]] bool hasFlag(std::uint64_t bit) const { return m_fighter.hasFlag(bit); }
    /// Sets (`on`) or clears the bits of `bits`.
    void setFlag(std::uint64_t bits, bool on) { m_fighter.setFlag(bits, on); }
    /// The scripts' state on it.
    [[nodiscard]] ScriptState& script() { return m_script; }
    [[nodiscard]] const ScriptState& script() const { return m_script; }
    /// The game time its updates have reached, whole ms (combat's clock).
    [[nodiscard]] std::uint64_t nowMs() const { return m_updates * 1000 / 30; }
    /// Alive and up (`HuIsAlive`): health left and not arrested. **Coney's reading** of the down states
    /// (`0x180050000`, not all researched): a knockdown that it gets up from still counts as alive.
    /// @orig 0x00235628 Human_IsAlive (unknown)
    [[nodiscard]] bool alive() const {
        return !m_fighter.health().depleted() && !m_script.arrested && !m_script.knockedOut;
    }
    /// Health as a percentage of its maximum, 0-100 (`HuGetHealthPercent`).
    /// @orig 0x00237c38 Human_GetHealthPercent (unknown)
    [[nodiscard]] float healthPercent() const { return m_fighter.health().fraction() * 100.0F; }
    /// A stun of `durationMs` from `nowMs` in the stun's loop (Fighter::stunFor()).
    void stunFor(std::uint64_t nowMs, std::uint64_t durationMs) { m_fighter.stunFor(m_animator, nowMs, durationMs); }
    /// Ends a stun now (Fighter::endStun()).
    void endStun(std::uint64_t nowMs) { m_fighter.endStun(nowMs); }
    /// `HuSetHealthPercent`: health becomes `percent` of the maximum in the PS2's single-precision arithmetic
    /// (rounded toward zero), truncated to whole points, so it lands one point under the exact share where that is
    /// whole (35 % of 900 is 314, 100 % is 899); a value outside (0, 100] gives full health.
    /// Research: docs/references/bindings/character.md#husethealthpercent
    /// @orig 0x002378a8 Human_SetHealthPercent (unknown)
    void setHealthPercent(float percent);
    /// `HuRevive` (Fighter::revive()), which ends in the heal: the body is made again, so it blocks other humans'
    /// walks once more (heal()).
    /// @orig 0x002377f8 Human_Revive (unknown)
    void revive() {
        m_fighter.revive(m_animator);
        heal();
    }
    /// What `Human_Heal` gives back beyond health: a fresh physics body (`Human_CreatePhysicsBody`), whose flags hold
    /// `BLOCKHUMANS`, so other humans' walks meet it again (docs/research/physics.md#human-contacts). The revive, a
    /// flash and a freed cuffed human's revive call it; waking up or a wound ending alone do not.
    /// @orig 0x0022eb40 Human_Heal (unknown)
    /// @orig 0x0021ce08 Human_CreatePhysicsBody (unknown)
    void heal() { m_blockHumansFlag = true; }
    /// `HuSetNormalMode` (Fighter::setNormal()); an arrest ends too.
    void setNormalMode(bool full);
    /// Breaks any pair it is in from outside (Fighter::breakPair()), as a placement does.
    void breakPair() { m_fighter.breakPair(); }
    /// The prop its clips made it carry (events `0x30`, `0x37`, `0x31`): Rembrandt's spray can in his power move 59;
    /// nothing when it carries none (docs/research/combat-grabs.md#power-move-props).
    [[nodiscard]] const std::optional<CarriedProp>& carriedProp() const { return m_carriedProp; }
    /// The carried prop goes (human `+0x348` and `+0x34c` emptied); the level removes its object. **Coney's
    /// stand-in**: the original also drops a held object of kind `0x27`, which no Coney human holds yet.
    /// @orig 0x0024ce40 Human_ReleaseCarriedProps (unknown)
    void releaseCarriedProps() { m_carriedProp.reset(); }
    /// What binding the human to a scene role does to it first, `HuSetNormalMode(human, true)`'s reset
    /// (docs/research/scenes.md#join-reset): a throw's aim is cancelled, any pair it is in is broken (breakPair()) so
    /// that a grab or a paired move does not keep it out of the scene, a wound ends, then setNormalMode(true) ends a
    /// grab, a stun, its rage and a fall and stands it in its idle with at least 1 health, and last its carried prop
    /// goes; the players' slow motion ends with setNormalMode() (takeSlowMotionReset()). Every other human's rage ends
    /// too, which is the caller's (endAllRage()). **Coney stand-in**: the steps for what Coney does not model (fire, a
    /// mini-game, an overlay task, a held kind-`0x27` or `0x10` object, a climb's body, a picked object) are left out.
    /// @orig 0x002325e0 Human_ResetForMode (unknown)
    void resetForJoin();
    /// `HuSetArrested`: arrested, the human stops where it is, its fighting ends and it loops 320
    /// `ANIM_ARRESTED_IDLE`; released, it stands again with the idle (docs/research/crimes.md#arrest).
    /// @orig 0x00237700 Human_SetArrested (unknown)
    void setArrested(bool arrested);
    /// Into or out of a throw's aiming state (docs/research/objects.md#throws): in, it stands (the stick no longer
    /// walks it) and plays 466 `gen_1hand_throw_enter` into 469 `gen_1hand_throw_cycle`; out, the style goes and the
    /// controller chooses again (no code plays 468). The aim itself is combat::ThrowAimState's.
    /// @orig 0x00227b30 Human_EnterThrowAim (unknown)
    /// @orig 0x00227a90 Human_ExitThrowAim (unknown)
    void setThrowAiming(bool aiming);
    /// One aiming frame's outcome: the aim's `heading` (the move function writes his facing) and whether the stick
    /// turned him, which plays 470 `gen_1hand_throw_turn` instead of 469.
    void aimThrow(float heading, bool turning);
    /// `HuSetWounded`: wounding (when not wounded already) ends its fighting, grab or throw, cuts its health to a
    /// quarter of the maximum and stamps the time 14 s on; healing only clears the mark. The caller flushes the brain.
    /// @orig 0x00237628 Human_SetWounded (unknown)
    /// @orig 0x0022fa78 Human_StartWounded (unknown)
    /// @orig 0x0022fc00 Human_EndWounded (unknown)
    void setWounded(bool wounded);
    /// `HuSetFullRage`: the rage meter full, held for `holdMs` before it decays.
    void fillRage(int holdMs) { m_fighter.combat().rage().fill(nowMs(), holdMs); }
    /// `HuSetRageFrac`: the rage meter at `fraction` of its maximum.
    void setRageFraction(float fraction) { m_fighter.combat().rage().setFraction(fraction, nowMs()); }
    /// `HuSetLockedRage`: flag::kRageLocked, and the meter locked or unlocked at once.
    void setRageLocked(bool locked);
    /// `HuSetRageMode`: `on` starts rage whatever the meter holds (RageMeter::force()); off ends it when it is on.
    /// The rage handlers (`CfgRageHandlers`' enter and exit functions, `SetRageMode` and `ClrRageMode` from
    /// `global.lua`) run from the scripted humans' step as for a rage the pad starts (ai::ScriptedHumans::
    /// runRageHandlers()), not inside the call as in the original. **Coney stand-in**: the notice to the player's
    /// camera is not built; the HUD follows the meter by itself (docs/research/scripting.md#level5).
    /// @orig 0x00237128 Human_SetRageMode (unknown)
    void setRageMode(bool on);

    /// Another human's grab catches this one (Fighter::catchInGrab()).
    void catchInGrab(const GrabCatch& grab) { m_fighter.catchInGrab(grab); }
    /// The grabber's numbers this update, while held (Fighter::updateGrabber()).
    void updateGrabber(const combat::GrabberState& grabber) { m_fighter.updateGrabber(grabber); }
    /// The grabber lets go (Fighter::releaseFromGrab()).
    void releaseFromGrab() { m_fighter.releaseFromGrab(m_animator); }

    // Triangle's context action and the pick-up (docs/research/crimes.md#triangle,
    // docs/research/combat-targets.md#breakables).

    /// What triangle tries after a climb (step 4 and 5 of `Player_TriangleAction`): the level's search for a context
    /// record or a loose object, which starts what it found on the human (or drops what he holds) and returns true.
    /// None: nothing.
    using ContextAction = std::function<bool(Human& human)>;
    /// Sets the level's context action (ContextAction).
    void setContextAction(ContextAction action) { m_contextAction = std::move(action); }
    /// What triangle tries before the level's context action: the kind-0 record, a cuffed human to free, comes first
    /// in the kinds' order (docs/research/crimes.md#triangle). None: nothing.
    void setFirstContextAction(ContextAction action) { m_firstContextAction = std::move(action); }
    /// What an airborne body touches before the walls push it (`Human_OnContact`'s object branch and the jump's strike
    /// shapes, docs/research/objects.md#pane-break): the level breaks the panes whose bodies a sphere at `centre` of
    /// `radius` reaches. Humans::setBodyContact() gives it; null: nothing.
    using BodyContact = std::function<void(Human& human, anim::Vec3 centre, float radius)>;
    /// Sets what its airborne body touches (BodyContact; not owned, null for nothing).
    void setBodyContact(const BodyContact* contact) { m_bodyContact = contact; }
    /// What a walking body slides along beyond the level's walls: the world objects' `BLOCKHUMANS` bodies
    /// (`Human_OnContact` slides along an object as along the level, docs/research/physics.md#contacts). Given the
    /// walking sphere's `centre`, `radius` and the update's `move`, the horizontal push out of the body it is deepest
    /// in, or nothing; `walker` is the human, whose gait decides what else the contact does (a sprinter strikes a
    /// `RUNTARGET` body). Humans::setObjectPush() gives it; null: nothing.
    using ObjectPush =
        std::function<std::optional<anim::Vec3>(const Human& walker, anim::Vec3 centre, float radius, anim::Vec3 move)>;
    /// Sets what it slides along beyond the walls (ObjectPush; not owned, null for nothing).
    void setObjectPush(const ObjectPush* push) { m_objectPush = push; }
    /// The humans its walking body meets (docs/research/physics.md#human-contacts): the characters' step gives every
    /// human its list each update (Humans::update()); the list may hold this human and null slots, which are skipped.
    /// Empty (the default): it meets none.
    void setHumanContacts(std::span<Human* const> humans) { m_otherHumans = humans; }
    /// Whether its capsule blocks other humans' walks: body flag `0x4` `BLOCKHUMANS`
    /// (docs/research/physics.md#human-contacts). A human loses it when knocked out (by a script or his health running
    /// out), wounded or arrested, and gets it back only from heal() (a new body) or the release from an arrest: one
    /// woken by `HuSetConscious(true)` or whose wound `HuSetWounded(false)` ended stays passable until then. Out of
    /// health or out of the world, he never blocks.
    /// @orig 0x00230328 Human_KnockOut (unknown)
    /// @orig 0x0022fa78 Human_StartWounded (unknown)
    /// @orig 0x002674c0 Human_OnHealthOut (unknown)
    /// @orig 0x0022ec18 Human_Arrest (unknown)
    /// @orig 0x0022ef58 Human_Unarrest (unknown)
    [[nodiscard]] bool blocksHumans() const;
    /// Takes `BLOCKHUMANS` away (blocksHumans()): a knock-out, a wound, the health running out or an arrest.
    void loseBlockHumans() { m_blockHumansFlag = false; }
    /// Whether it and `other` are linked, so their walks pass through each other: one holds the other in a grab, a
    /// tackle, a mount or a mugging (the link's group number at body `+0x3c`, `Grab_Link` and the rest), or one frees
    /// the other from his cuffs (`Uncuff_BeginMash`). **Coney stand-in**: the scenes' shared group is not set.
    /// @orig 0x0022c730 Grab_Link (unknown)
    [[nodiscard]] bool linkedWith(const Human& other) const;
    /// Its push weight (attribute 7, `+0x27c`): 1e9 while it must not move (linked in a pair, arrested, or freeing a
    /// cuffed human), else 1.
    /// @coney-only a read of the weight the links and the arrest give (the original keeps it at attribute 7)
    [[nodiscard]] float pushWeight() const;
    /// Whether another human's walk pushes it: not when a script made it unpushable (`HuSetPushable(false)`, body flag
    /// `0x200`), when it is only slid along.
    [[nodiscard]] bool pushable() const { return m_script.pushable; }
    /// The push-out other humans' contacts left for its next move (body `+0x50`), added to that move and cleared.
    [[nodiscard]] anim::Vec3 pendingPush() const { return m_pendingPush; }
    /// Adds `push` to pendingPush() (a contact's code 4, human::contactPush()).
    void addPush(anim::Vec3 push) { m_pendingPush = anim::add(m_pendingPush, push); }
    /// The breakable objects square may strike from now on (the level's whole panes), given each step.
    void setObjectTargets(std::vector<ObjectTarget> objects) { m_objectTargets = std::move(objects); }
    /// Picks up the object `handle` at `point` with clip `clip` (world_objects::pickupClip()): the clip plays after a
    /// world_objects::kPickupBlend blend, holding the grab bit `0x10` so the human neither moves nor acts, and the
    /// human turns to the object over the time to the clip's first event, when the object is taken (takePickedUp()).
    /// Returns false, doing nothing, when the anim set lacks the clip. **Coney's stand-ins**: the turn for
    /// `0x00275d10`'s steer (inferred to be like the attack's, without its slide); the grab bit for what the clip
    /// holds.
    /// @orig 0x0025e5a8 Human_PickUpMessage (unknown)
    bool startPickUp(double handle, anim::Vec3 point, std::uint32_t clip);
    /// Starts the stereo theft at the stereo at `point`: the human turns to it at once and the mini-game runs (mode 3)
    /// with `stageTurns` turns of the stick a stage (combat::stereoStageTurns()). **Coney stand-in**: the turn is made
    /// at once and he stays put, where the original's Human_AlignToObject turns and moves him over 0.1 s.
    void startStereoTheft(anim::Vec3 point, float stageTurns);
    /// Enters the step control for one step toward `heading` (radians) with the turn boost `boost`: the next state
    /// update that finds the human free plays the step's clip (stepClipFor(); the fight stance's in the fight
    /// stance), turns him to its end facing over half the clip, and leaves the control. Until then he waits, not
    /// moved by his stick or brain. Refused (false) while grabbed, grabbing or held in a hold, or when a step is
    /// already waiting. **Coney's reading**: those stand for the state bits `0x180f3ff0`.
    /// @orig 0x002432b0 Human_EnterStepControl (unknown)
    bool enterStepControl(float heading, int boost);
    /// Whether the step control still waits to play its step.
    [[nodiscard]] bool stepControlWaiting() const { return m_stepRequest.has_value(); }
    /// Whether a step clip or a turn clip holds the record (`0x20080000`): a step under way.
    /// @orig 0x00228448 Human_HasHeld20080000 (unknown)
    [[nodiscard]] bool stepHeld() const;
    /// Leaves the step control: a step still waiting is dropped (one already playing plays on).
    /// @orig 0x00243360 Human_LeaveStepControl (unknown)
    void leaveStepControl() { m_stepRequest.reset(); }
    /// Whether the human is idle under its own control (the stander test of giving way): on the ground, not climbing,
    /// in no wheelchair, not held by combat or a grab, and no held flag at all on the record.
    /// @orig 0x00225390 Human_IsIdleUnderControl (unknown)
    [[nodiscard]] bool idleUnderControl() const;
    /// Whether the stereo theft's intro or loop (683, 684) is playing. While a theft runs, anything else taking the
    /// body (a hit, a knock-down, death, a scene) ends it with no outcome at the next update (`MiniGame_Abort`, from
    /// the hit `0x00268c50` and from leaving normal mode `0x002325e0`, docs/research/crimes.md#uncuffing).
    /// @coney-only a read of the clip playing
    [[nodiscard]] bool stereoTheftPlaying() const;
    /// Starts freeing `cuffed` by the mash (`Uncuff_Start`, docs/research/crimes.md#uncuffing): the cuffed human's
    /// steer is dropped and this human is steered onto him for 325 (Fighter::steerUncuff(),
    /// docs/research/crimes.md#uncuff-pose); after a 0.01 s blend he plays 325 `ANIM_ARREST_RELEASE_INTRO_FRONT`,
    /// which holds `0x2000000` and so keeps the mash's input closed, then loops 329; the two are linked until
    /// endUncuff() (linkedWith(), pushWeight(): `Uncuff_BeginMash`'s shared body group and push weight 1e9); the mash
    /// (mode 1) runs with the Warrior factor `mashFactor` (combat::mashFactor()).
    /// @orig 0x00260ca8 Uncuff_Start (unknown)
    /// @orig 0x0022d3f8 Uncuff_BeginMash (unknown)
    void startUncuff(Human& cuffed, float mashFactor);
    /// Whether the freer's clips (325 or 329) still play: false once a hit or anything else took the body.
    /// @coney-only a read of the clip playing
    [[nodiscard]] bool uncuffPlaying() const;
    /// The freer's end: the mash stops, the link to the cuffed human goes (`MiniGame_Abort` gives the freer his own
    /// body group and push weight 1 again) and he plays 332 `ANIM_ARREST_RELEASE_END`, or 331
    /// `ANIM_ARREST_RELEASE_HIT_REACT` after a hit, then his idle.
    /// @orig 0x0022d628 MiniGame_Abort (unknown)
    void endUncuff(bool hit);
    /// The cuffed human's half while `freer` frees him: 326 paired to the freer after a 0.01 s blend, then the loop
    /// 330; he is not moved.
    /// @orig 0x00260ca8 Uncuff_Start (unknown)
    void playUncuffReact(const Human& freer);
    /// The cuffed human's end: freed, 333 `ANIM_ARREST_RELEASE_END_REACT` paired to `freer` then his idle; still
    /// cuffed, back to 320.
    /// @orig 0x002606e8 Uncuff_MashSuccess (unknown)
    /// @orig 0x00260a70 Uncuff_MashFail (unknown)
    void endUncuffReact(const Human& freer, bool freed);
    /// The reviver's half of a revive (docs/research/combat-reactions.md#revive): the led steer turns him to `downed`'s
    /// head and slides him to 351's reach from it over 351's time to its first event + 0.1 s (Fighter::steerRevive()),
    /// and he plays 351 `gen_revive_begin`, then 353 `gen_revive_end`, then his idle (through 389 out of the fight
    /// stance, the fight idle in it), after a 0.01 s blend, each clip holding `0x8000000`.
    /// **Coney stand-in**: the pair ids, the two calls on both humans before the say, the push sphere's switch-off
    /// and Human_SetPartner are not built; the say is the game mode's (GameplayMode::tryRevive()).
    /// @orig 0x002752a0 Revive_Start (unknown)
    void startRevive(const Combatant& downed);
    /// Whether 351 `gen_revive_begin` plays: the revive's first clip, at whose end the flash is spent.
    /// @coney-only a read of the clip playing
    [[nodiscard]] bool reviveBeginPlaying() const;
    /// Whether 353 `gen_revive_end` plays: 351 ended and the reviver goes on to his idle.
    /// @coney-only a read of the clip playing
    [[nodiscard]] bool reviveEndPlaying() const;
    /// The revived human's half: healed in full (wounding over, his health back, up from the ground, as `HuRevive`)
    /// and up with 354 `gen_revive` paired to `reviver` holding `0x8000`, then 389 and his idle, or with `reviver` in
    /// the fight stance straight into the fight idle.
    /// @orig 0x00274ee0 Revive_Finish (unknown)
    void playRevived(const Human& reviver);
    /// Turns to face `point` over `seconds` of updates, whatever plays (`Human_TurnToFacePoint`); the human stays where
    /// he stands. A Warrior handed a swap turns to the presser this way.
    /// @orig 0x00221c20 Human_TurnToFacePoint (unknown)
    void turnToFace(anim::Vec3 point, float seconds);
    /// Starts a tag's spray clips (`Tag_StartSprayClips`): 334 `ANIM_TAGGING_INTRO`, then the loop 335, turning to face
    /// the tag at `point` over half of 334's length (`Human_TurnToFacePoint`); the human stays where he stands.
    /// 334 holds the busy bit `0x400`, so the stance's movement sets no velocity while it plays; and while any spray
    /// clip plays (tagSprayPlaying()) the stance, the commands and the anim-state choice wait.
    /// Returns false when the intro clip is not loaded (he then turns to the tag at once).
    /// docs/research/crimes.md#tag-look, docs/research/scripting.md#level87-fixes (fix 2)
    /// @orig 0x00278018 Tag_StartSprayClips (unknown)
    bool startTagSpray(anim::Vec3 point);
    /// A pause of the stick game began (a slip or a new charge): 337, then the loop 335 again.
    /// @orig 0x00278210 Tag_RestartSprayClips (unknown)
    void restartTagSpray();
    /// Whether the spray's intro is over and its loop plays: the moment the stick game goes live (the intro's end hook
    /// `0x00277ed8`).
    /// @coney-only a read of the clip playing (the original's intro end hook is where the loop starts)
    [[nodiscard]] bool tagSprayLooping() const;
    /// Whether the spray's clips still play (the intro, the loop or the restart): false once something else took the
    /// body.
    /// @coney-only a read of the clip playing
    [[nodiscard]] bool tagSprayPlaying() const;
    /// The spray is over (finished or let go): a spray clip still playing hands over to 336 and the idle, or the fight
    /// idle in a fight stance (HumanAnimator::playTagEnd()). A spray cut short does not come here: it plays no end
    /// clip.
    /// @orig 0x002783b0 Tag_EndSprayClips (unknown)
    void endTagSpray();
    /// Whether a pick-up is under way.
    [[nodiscard]] bool pickingUp() const { return m_pickUp.has_value(); }
    /// The object whose pick-up reached its clip's event since the last call, once; nothing otherwise.
    [[nodiscard]] std::optional<double> takePickedUp() { return std::exchange(m_pickedUp, std::nullopt); }
    /// Whether the clip playing passed its release event (type 10, which every throw clip has once) since the last
    /// call: the throw lets go of the held object then (`Human_ReleaseThrow`, docs/research/objects.md#held).
    [[nodiscard]] bool takeThrowRelease() { return std::exchange(m_throwRelease, false); }

  private:
    // A climb under way: what it climbs, which clip of its chain plays, and the move to its start point.
    struct ClimbRun {
        std::uint32_t firstId = 0;
        bool running = false;
        std::uint32_t phase = 0;  // 0, 1 or 2: the clip of the chain playing
        int moveUpdates = 0;      // updates left of the move to the start point
        bool clipChanged = false; // the chain went on to its next clip this update: the body does not move
        anim::Vec3 start;         // the start point (feet)
        bool over = false;        // record +0x08 bit 0x40: fences do not block the body
    };

    // The locomotion: target speed, skid, turn, acceleration; sets the horizontal velocity, none while `gated`.
    // @orig 0x00240e38 Human_PlayerLocomotion (unknown)
    void locomote(bool gated);
    // Gives the animator the gait-speed factors now: the player's Movement tunables and this human's speed scale.
    void refreshSpeedFactors();
    // A brain-driven human's locomotion (no pad): the brain's speed approached at the AI's rates, the facing turned to
    // the brain's heading by a constant step, no skid. Sets the horizontal velocity, none while `gated`.
    // @orig 0x00243848 Human_UpdateControl (unknown)
    void aiLocomote(bool gated);
    // `wheelchairControl` for a pad-driven human with the wheelchair flag: L1 and R1 push, one alone turns, cross
    // brakes; the sticks are not read (docs/research/characters.md#wheelchair).
    void wheelchairControl(bool locked);
    // The jump's state function: the heading turns toward the stick at the air limit, the speed is kept.
    // @orig 0x00240898 Human_AirControl (unknown)
    void airControl();
    // Adds the pose's root motion (turned into world axes) to the velocity and its turn to the heading.
    // @orig 0x0023f238 Human_ApplyRootMotion (unknown)
    void applyRootMotion(const anim::Pose& pose);
    // Moves by the velocity against the walls, then snaps to the ground or starts a fall.
    // @orig 0x0023d8c8 Human_Move (unknown)
    void moveOnGround(const raycast::CollisionMesh& mesh);
    // Moves a falling human, landing on a floor it passed on the update before.
    // @orig 0x0023d8c8 Human_Move (unknown)
    void moveInAir(const raycast::CollisionMesh& mesh);
    // Where a walking sweep ends: the feet, and the velocity the sweep wrote back to the human (the slid one for a
    // player, zero when the body was stuck), or nothing when it wrote none.
    struct Swept {
        anim::Vec3 feet;
        std::optional<anim::Vec3> velocity;
    };
    // Sweeps the walking body from the feet at m_position by `velocity` over the update plus the pending `push`: the
    // other humans first when `meetOthers`, then the level through the physics world's sweep (physics::sweepBody,
    // docs/research/physics-bodies.md#u3-sweep), then the world objects' bodies. The body's stuck counter is
    // m_blockedUpdates.
    // @orig 0x0033e278 PhysicsBody_Sweep (unknown)
    [[nodiscard]] Swept sweep(const raycast::CollisionMesh& mesh, anim::Vec3 velocity, anim::Vec3 push,
                              bool meetOthers);
    // The humans the walking capsule meets moving from the feet at `from` by `move` (docs/research/physics.md
    // #human-contacts), up to three passes: the nearest contact stops the capsule at it, the rest of the move slides
    // along the other (code 1), and, unless this capsule strikes (a charge), the other is given half of the pass's move
    // into him as his pending push (code 4). Returns the move left, which the walls then take; each contact's normal
    // goes to m_contacts.
    // @orig 0x0033d498 PhysicsBody_SweepHumanMask (unknown)
    [[nodiscard]] anim::Vec3 meetHumans(anim::Vec3 from, anim::Vec3 move);
    // The airborne body's push-out after its move to `feet`: its sphere (a player's larger one) out of the nearest wall
    // it overlaps, added to the pending push for the next move.
    // @orig 0x0021a490 Human_PushOutInAir (unknown)
    void pushOutInAir(const raycast::CollisionMesh& mesh, anim::Vec3 feet);
    // The ground snap from the feet at `feet`: on a hit the feet go onto it; a miss starts a fall.
    // @orig 0x0023eab8 Human_SnapToGround (unknown)
    void snapToGround(const raycast::CollisionMesh& mesh, anim::Vec3 feet);
    // Lands: clears the airborne state and the vertical speed; a jump's landing plays its clip.
    // @orig 0x0023e090 Human_Land (unknown)
    void land(anim::Vec3 feet);
    // The materials the body and the snap pass through now: the fences while climbing over.
    // @orig 0x00219d50 Human_OnContact (unknown)
    [[nodiscard]] std::span<const std::uint8_t> passThrough() const;

    // The stick step is skipped (combat's states, or the human is busy): the clip and an attack's slide move the body;
    // a block turns toward the stick in place (the shuffle); a standing grab turns and walks the pair by the stick.
    // @orig 0x00240e38 Human_PlayerLocomotion (unknown)
    void holdForCombat();
    // Locked onto `target`: a pad-driven human faces it and walks at the combat walk's speed along the stick, in the
    // combat-walk clip of the stick's angle from the facing; with the velocity `gated`, it only faces the target and
    // the clip playing goes on. A human no pad drives runs aiCombatWalk() instead.
    // @orig 0x00241b90 Human_FightStanceMove (unknown)
    void combatWalk(const Combatant& target, bool gated);
    // The AI's fight stance (docs/research/characters.md#ai-locomotion): the facing turns to the brain's heading at
    // aiStanceTurn() while the move asks for a speed (always when wounded), and does not follow the target; the feet
    // strafe along the line to the move's point at the stance walk (the walk when wounded) when it asks for more than
    // 0.1, else stand. With the velocity `gated`, it only turns.
    // @orig 0x00243848 Human_UpdateControl (unknown)
    void aiCombatWalk(bool gated);
    // Combat's update from the record: the stick turned into the facing frame, the game time, the targets; and when the
    // fighter entered the fight stance, the end of the hiding (Player_EnterStance's Human_ClearHiddenState half).
    // @coney-only glue: combat's input from the record for Fighter::update(), and the stance entry's end of hiding
    void fight(std::span<Combatant* const> targets, const raycast::CollisionMesh* mesh,
               std::span<Combatant* const> calm);
    // The attacker's side of a warning: an event 0x24 or 0x26 of the clip playing that this step's animation passed
    // (since `beforeTime` of `before`, which played `beforeId`, when it is still the clip) tells the fighter's target,
    // when it stands within twice the anim's reach.
    // @orig 0x00101dd8 Anim_FireFrameEvents (unknown)
    void sendWarnings(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // Notes the slow-motion event (0x2e or 0x2f) this step's animation fired, the last when several did: from the
    // events the task stack fired (anim::AnimTaskStack::firedEvents()), so a clip that hands over in the step counts
    // up to its end, at the frames the held flags' events use (anim::eventFrame()).
    // @orig 0x00101dd8 Anim_FireFrameEvents (unknown)
    void noteSlowMotion();
    // Switches the strike shapes by the events of the clip playing that this step's animation passed (as the held
    // flags' events fire, anim::eventFrame()). **Coney choice**: when another clip takes the top before the one that
    // switched them on ends its window, they go off, as its own off event would have switched them.
    // @orig 0x00101dd8 Anim_FireFrameEvents (unknown)
    void noteStrikeEvents(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // Notes the release event (type 10) of the clip playing when this step's animation passed it, as
    // noteStrikeEvents() does; takeThrowRelease() reads it.
    // @orig 0x00101dd8 Anim_FireFrameEvents (unknown)
    void noteReleaseEvent(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // Notes the action event (0x41) of the clip playing that this step's animation passed (message 0xc1, read by
    // Human_HandleMessage at 0x002473bc).
    // @orig 0x00101dd8 Anim_FireFrameEvents (unknown)
    void noteActionEvent(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // Acts on the carried prop's events of the clip playing that this step's animation passed, as noteStrikeEvents()
    // finds its own: 0x30 makes the prop when it carries none, 0x37 moves it to the event's attach point and pose
    // (message 0xb7), 0x31 removes it when it is of the named type (docs/research/combat-grabs.md#power-move-props).
    // @orig 0x00101dd8 Anim_FireFrameEvents (unknown)
    void noteCarriedPropEvents(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // The world placement of the body the strike shapes are posed on.
    // @coney-only the placement Coney's strike shapes are posed with
    [[nodiscard]] BodyPlacement placement() const;
    // Stamina's drain and refill, then the sprint flag, for this update's L2.
    // @orig 0x002562d0 Human_UpdateMeters (unknown)
    // @orig 0x00256a60 Human_UpdateDown (unknown)
    void updateMeters(bool sprintHeld);
    // The hidden state's own ends each update: a sprint, the fight stance or the grace running out
    // (docs/research/stealth.md#hidden).
    // @orig 0x00254e78 Human_UpdateActions (unknown)
    // @orig 0x0027ce90 Player_UpdateSprint (unknown)
    void updateHiding();
    // Triangle: a climb (stick above the dead zone), then the context action, then a jump.
    // @orig 0x0027c120 Player_UpdateActions (unknown)
    void tryActions(const raycast::CollisionMesh* mesh, bool sprintHeld);
    // The context action triangle tries after a climb: the level's (setContextAction()), which also drops what is in
    // hand when there is nothing to take. Returns whether it did something.
    // @orig 0x002811f0 Player_TriangleAction (unknown)
    [[nodiscard]] bool tryContextAction();
    // A pick-up's step after the animation's: the turn, and the take at the clip's event; a clip replaced ends it.
    // @orig 0x0025e5a8 Human_PlayPickupClip (unknown)
    void followPickUp();
    // Tries a climb toward `direction`; starts it and returns true on success.
    // @orig 0x002826f0 Climb_TryStart (unknown)
    // @orig 0x00281c20 Climb_Start (unknown)
    [[nodiscard]] bool tryClimb(const raycast::CollisionMesh& mesh, anim::Vec3 direction);
    // Tries a jump; launches it and returns true on success.
    // @orig 0x002829e8 Player_TryJump (unknown)
    // @orig 0x002217f0 Human_LaunchJump (unknown)
    [[nodiscard]] bool tryJump(const raycast::CollisionMesh* mesh, anim::Vec3 direction);
    // A climb's work as its clips change: the probe and move at the second clip, the end.
    // @orig 0x00281450 Climb_OnFirstClipEndRun (unknown)
    // @orig 0x00281838 Climb_OnFirstClipEndStand (unknown)
    void followClimb(const raycast::CollisionMesh* mesh);
    // Everything a human just made starts with, and what a settle drops (spawn(), settle()): no motion, traversal,
    // stance, attack or stun, the idle; with `keepMeters` the health and its maximum, stamina, power and rage stay.
    // @orig 0x00218008 Human_Init (unknown)
    void standAfresh(bool keepMeters);
    // Ends a climb.
    // @coney-only the end of Coney's climb bookkeeping; the original's chain ends through its clips' hooks
    void endClimb();
    // The state update while flying (setFlight()): the move by the stick and the climb, through everything.
    void fly();

    PlayerRecord m_record;
    ScriptState m_script;
    HumanAnimator m_animator;
    std::string m_idleClipName;                           // setIdleClip()
    std::map<std::uint32_t, std::string> m_overrideNames; // setOverrideClip()
    std::unique_ptr<combat::AnimRangeList> m_ownRanges;   // the list with the class's damage, when it has one
    std::vector<std::int16_t> m_classDamage;              // the class's damage table, for HuApplyDamageModifier
    int m_characterClass = -1;                            // human +0xcc, from its brain
    int m_damagePercent = 0;                              // a player's Warrior class percentage (0: not a player)
    const combat::AnimRangeList* m_ranges;
    FighterProfile m_profile;
    Fighter m_fighter;
    std::string m_logName;                          // setLogName()
    std::vector<anim::Vec3> m_announced;            // attacks announced since the brain last looked (event 0x10)
    std::optional<std::uint16_t> m_slowMotionEvent; // the last update's slow-motion event (0x2e / 0x2f)
    bool m_slowMotionEnd = false;                   // endSlowMotion(), takeSlowMotionEnd()
    bool m_slowMotionReset = false;                 // setNormalMode(), takeSlowMotionReset()
    std::optional<std::uint32_t> m_actionEvent;     // the anim whose action event (0x41) the last update passed
    float m_stepSeconds = kStepSeconds;             // the characters' step (slow motion shortens it)
    std::uint64_t m_updates = 0;                    // updates stepped: combat's game time
    std::array<anim::Quat, anim::kPoseBones> m_bindRotations{};
    FaceState m_faceState;            // the record's +0x1c (+0x24 is m_face's current())
    FaceLayer m_face;                 // the instance's queue: the face clips
    bool m_speaking = false;          // a line of its is alive (setSpeaking()), as +0x170
    FaceRules* m_faceRules = nullptr; // setFaceRules(), not owned
    float m_scale = 1.0F;
    float m_configuredScale = 1.0F;        // the scale the human was made with
    float m_meshScale = 1.0F;              // setMeshScale()
    std::optional<float> m_collisionFixed; // the scale the collision keeps while it does not follow the mesh
    float m_speedScale = 1.0F;             // setSpeedScale()
    float m_animRate = 1.0F;               // setAnimRate()
    anim::Vec3 m_position;
    float m_heading = 0.0F;
    anim::Vec3 m_velocity; // z is the vertical speed (+0x3a0)
    StickIntent m_intent;
    float m_lastMagnitude = 0.0F;
    StickIntent m_lastStick; // the stick's last pushed angle, its magnitude fading once let go (+0x5d8, +0x5dc)
    std::optional<float> m_moveSpeed; // a brain's move speed this update, in place of the stick's target speed
    TurnState m_turn;
    bool m_airborne = false;
    bool m_outOfWorld = false;
    bool m_blockHumansFlag = true; // body flag 0x4 BLOCKHUMANS, on with every new body
    bool m_aimTurning = false;     // a throw aim's turn clip (470) plays
    bool m_landingPending = false; // an airborne move passed a floor: land once the feet start kLandingDepth below it
    float m_landingFloorZ = 0.0F;  // that floor's height where it was passed
    float m_landingSpeed = 0.0F;   // the vertical speed it was passed at
    std::uint32_t m_airborneUpdates = 0;
    std::uint32_t m_blockedUpdates = 0;
    anim::Vec3 m_lastGround;
    anim::Vec3 m_groundNormal{0.0F, 0.0F, 1.0F};
    bool m_coveredGround = false;                  // onCoveredGround()
    std::uint8_t m_groundMaterial = 5;             // groundMaterial(): CONCRETE until the first snap
    bool m_reportSounds = false;                   // reportSounds()
    std::vector<HumanSound> m_contactSounds;       // reportSound()
    std::optional<CarriedProp> m_carriedProp;      // carriedProp(): +0x348, +0x34c
    std::uint16_t m_groundFlags = 0;               // the last ground snap's triangle flags
    bool m_hidden = false;                         // state 0x200000
    std::optional<Flight> m_flight;                // setFlight(): flying, and how
    std::optional<std::uint64_t> m_hideGraceUntil; // record +0x114: the grace's end, ms
    float m_lastLandingSpeed = 0.0F;
    std::uint32_t m_landings = 0;
    Stamina m_stamina;
    bool m_sprinting = false;
    bool m_jumping = false;
    float m_lean = 0.0F;
    float m_lastTurn = 0.0F; // the heading's change in the last state update, for the lean
    // The gait of the velocity the last update left, which the dispatcher's gait tests read (fight()).
    Gait m_gaitBefore = Gait::Standing;
    std::optional<ClimbRun> m_climbRun;
    // The step control's waiting step: its heading and turn boost (enterStepControl()).
    struct StepRequest {
        float heading = 0.0F;
        int boost = 0;
    };
    std::optional<StepRequest> m_stepRequest;
    // Plays the waiting step when the human is free; true while the step control holds the update.
    // @orig 0x00243420 Human_StepControl (unknown)
    bool runStepControl();
    // A pick-up under way: the object, the clip, the updates to its event and the turn each takes.
    struct PickUpRun {
        double handle = 0;
        std::uint32_t clip = 0;
        int updatesLeft = 0;
        float turnStep = 0.0F;
    };
    std::optional<PickUpRun> m_pickUp;
    // A spray intro's turn to the tag: the updates left and the turn each takes.
    // A turn to a point spread over a clip (the spray's intro, the uncuffing's 325): the clip, the updates left and
    // the turn each takes; it ends early when the clip is replaced.
    struct ClipTurn {
        std::uint32_t clip = 0; // 0: not tied to a clip
        int updatesLeft = 0;
        float turnStep = 0.0F;
    };
    std::optional<ClipTurn> m_clipTurn;
    // The cuffed human this one is freeing, from startUncuff() to endUncuff(): the two are linked (linkedWith()) and
    // this one's push weight is 1e9 (pushWeight()), as Uncuff_BeginMash's shared body group and attribute 7 make them.
    const Human* m_uncuffing = nullptr;
    // Starts a turn to `point` spread over `share` of `clip`'s playing time; nothing when the clip is not loaded.
    // @orig 0x00221c20 Human_TurnToFacePoint (unknown)
    void turnOverClip(std::uint32_t clip, anim::Vec3 point, float share);
    std::optional<double> m_pickedUp; // the object a pick-up reached, until takePickedUp()
    bool m_throwRelease = false;      // a release event passed, until takeThrowRelease()
    ContextAction m_contextAction;
    ContextAction m_firstContextAction;         // tried before m_contextAction (a cuffed human to free)
    const BodyContact* m_bodyContact = nullptr; // setBodyContact(), the step's
    std::span<Combatant* const> m_calm;         // setCalm(), for the next updateActions() only
    const ObjectPush* m_objectPush = nullptr;   // setObjectPush(), the step's
    const anim::Skeleton* m_skeleton = nullptr; // setSkeleton()
    StrikeShapes m_strikes;
    // The shapes on as they were posed the update before, in the world: each one's strike is tested along its move
    // since (sweptShapesMeet()). A shape just switched on has none, so its first update is tested where it stands.
    std::vector<PosedShape> m_strikesBefore;
    std::uint32_t m_strikeAnim = 0;            // the anim whose events switched the strike shapes on
    bool m_chargeStopped = false;              // the charge playing met a wall head-on (moveOnGround())
    std::vector<ObjectTarget> m_objectTargets; // the breakable objects square may strike
    std::optional<ClimbProbe> m_climbProbe;
    physics::MeshScratch m_meshScratch;    // the mesh queries' memory, reused between sweeps
    std::vector<anim::Vec3> m_contacts;    // the normals of the humans and objects the last sweep slid along
    std::span<Human* const> m_otherHumans; // setHumanContacts(), the step's
    anim::Vec3 m_pendingPush{};            // addPush(): others' pushes, for the next move
};

/// Ends the rage of every human in `humans` that rages, at once (Human::setRageMode() off, the meters left where they
/// are), as a scene's bind does whoever it binds (docs/research/scenes.md#join-reset). **Coney's reading**: the
/// research calls them every player (scenes.md) and every human in rage (characters.md); either way a raging human
/// stops, so the caller passes every human it has.
/// @orig 0x00232538 Humans_EndAllRage (unknown)
void endAllRage(std::span<Human* const> humans);

} // namespace coney::human
