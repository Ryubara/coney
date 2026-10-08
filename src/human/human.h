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
#include "human/climb.h"
#include "human/combatant.h"
#include "human/fighter.h"
#include "human/holdable.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"
#include "human/script_state.h"
#include "human/stamina.h"
#include "human/strike_shapes.h"
#include "human/target_human.h"
#include "human/victim.h"
#include "raycast/collision_mesh.h"

// A human driven through its per-player record, which a pad (the player) or a brain writes: its locomotion,
// sprint and stamina, jump and climbs, its fighting (Fighter), its standing on and falling from the level's collision
// mesh, and its animation, stepped at the characters' fixed 30 Hz in the original's three passes (animation, state
// update, actions; human/humans.h runs them across every human). Platform-neutral and deterministic: no clock, no
// randomness, so a scripted pad gives the same path on every run (test mode).
// Research: docs/research/characters.md, docs/research/tasks.md#humans-update

namespace coney::human {

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
    std::span<Combatant* const> targets;                ///< The humans that can be fought.
};

/// A brain's move (brain `+0x110`, `+0x114`): the heading and speed the locomotion follows in place of the stick for a
/// human a brain drives (docs/research/ai.md#moving).
struct BrainMove {
    float heading = 0.0F; ///< Radians, 0 facing +y.
    float speed = 0.0F;   ///< m/s; 0 stands.
    /// The brain's turn boost (`+0x0b`): its gait turn limits × (boost + 1), or ÷ (1 − boost) below 0.
    int turnBoost = 0;
};

/// A human's per-player record (`0x00660f50 + i × 0x2c`, docs/research/ai.md#brain): what drives it each update. A
/// pad writes it for the player (human::Player); a brain writes the command and the stick for an AI human, as a pad
/// would (docs/research/tasks.md#humans-update). The human reads nothing else of its input.
struct PlayerRecord {
    float stickX = 0.0F;        ///< Left stick, -1 (left) to 1 (right).
    float stickY = 0.0F;        ///< Left stick, -1 (down) to 1 (up).
    anim::Vec3 cameraForward;   ///< The view the stick is turned by (a brain gives the world's axes).
    bool sprintHeld = false;    ///< L2 held.
    bool actionPressed = false; ///< Triangle pressed this update.
    combat::CommandId command = combat::command::kNone; ///< The command the human acts on (`+0x20`).
    /// A brain's climb this update toward this direction (a route's climb leg: `Climb_TryStart` toward the waypoint,
    /// docs/research/ai.md#route-follow); a pad never sets it.
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
    Human(const characters::AnimSet& anims, const AnimSlots& slots,
          std::span<const anim::Quat, anim::kPoseBones> bindRotations, float scale = 1.0F,
          const combat::AnimRangeList* ranges = nullptr, std::span<const std::int16_t> classDamage = {},
          int damagePercent = 0);

    /// Makes it fight as `profile` says (a player, or a human no player controls with its class's power class and
    /// health), from now and at every spawn(); its fighting starts afresh. A human is a player until told otherwise.
    void setFighterProfile(const FighterProfile& profile);
    [[nodiscard]] const FighterProfile& fighterProfile() const { return m_profile; }

    /// Makes it another character where it is, as a human made with these arguments would be: it animates with
    /// `anims` (HumanAnimator::changeAnims()), fights with its own copy of `ranges` with `classDamage` scaled by
    /// `damagePercent` written over it (as the constructor takes them) and as `profile` says, its fighting afresh.
    /// What the human is to the rest of the game stays: its handle's place in the humans, its script state, its hooks
    /// and actions, its skeleton (set it again for another skeleton). Spawn it again to stand it up afresh.
    void changeCharacter(const characters::AnimSet& anims, const combat::AnimRangeList* ranges,
                         std::span<const std::int16_t> classDamage, int damagePercent, const FighterProfile& profile);

    /// Places the human at `position` (the feet, game axes) facing `headingDegrees` (0 faces +y), snapped to the
    /// ground of `mesh` (may be null: no snap) with a 2.5 m ray from 1 m above; 0.01 above the hit. Stamina is full
    /// again and anything in progress (a jump, a climb) is dropped.
    /// @orig 0x00218008 Human_Init (unknown)
    void spawn(const raycast::CollisionMesh* mesh, anim::Vec3 position, float headingDegrees);

    /// One update of 1/30 s for this human alone: `input` written into its record, then animate(), updateState() and
    /// updateActions() in turn. human::Humans runs the same passes across every human in the original's order.
    void step(const HumanInput& input, const raycast::CollisionMesh* mesh);

    /// The per-player record the next update reads; a pad or a brain writes it before the update.
    [[nodiscard]] PlayerRecord& record() { return m_record; }
    [[nodiscard]] const PlayerRecord& record() const { return m_record; }

    /// The update's first pass, the animation step: the gait the last update left is kept for the dispatcher's gait
    /// tests, then every task advances (the clips' events moving the record's
    /// `+0x08`), and a climb follows its clips. Nothing for a human out of the world.
    void animate(const raycast::CollisionMesh* mesh);
    /// The second pass, the state update: the record's stick turned by its camera; the locomotion (through the
    /// locomotion gate, human/locomotion_gate.h), the jump's air control or the climb's own motion, with the clip's
    /// root motion added; gravity while airborne; the move against `mesh`'s walls with the ground snap, or the fall
    /// and landing.
    /// @orig 0x00240e38 Human_PlayerLocomotion (unknown)
    /// @orig 0x0023fea8 Human_StateUpdate (unknown)
    void updateState(const raycast::CollisionMesh* mesh);
    /// The third pass, the actions: stamina and the sprint, combat's dispatcher from the record's command (against
    /// `targets`), triangle's climb or jump, the lean and the animation state.
    void updateActions(std::span<Combatant* const> targets, const raycast::CollisionMesh* mesh);

    /// The pose to draw now.
    [[nodiscard]] anim::Pose pose() const { return m_animator.pose(m_bindRotations); }

    /// The skeleton its strike shapes are posed on: its character's (not owned; it must outlive the human). Null (the
    /// default): it has no strike shapes, and its attacks land at their hit update (combat::attackHitUpdate()).
    void setSkeleton(const anim::Skeleton* skeleton) { m_skeleton = skeleton; }
    /// Which of its strike shapes its clips' events have switched on (human/strike_shapes.h), and what they struck.
    [[nodiscard]] const StrikeShapes& strikeShapes() const { return m_strikes; }
    [[nodiscard]] StrikeShapes& strikeShapes() { return m_strikes; }
    /// Its strike shapes posed in the world now: the ones switched on (and the capsule while its strike flag is on),
    /// or with `targets` the spine and the head, which a strike is tested against (flag `0x4`). Empty without a
    /// skeleton.
    [[nodiscard]] std::vector<PosedShape> posedStrikeShapes(bool targets) const;
    /// The body point (`+0x4e0`): the hips' (pose bone 2) position in the body's own frame, z up and facing +y, not
    /// turned by the heading, times the scale; none without a skeleton (docs/research/camera.md#body-point).
    /// @orig 0x0023bde8 Human_GetBoneTransform (unknown)
    [[nodiscard]] std::optional<anim::Vec3> bodyPoint() const;
    /// What a human's switched-on strike shapes meet beyond the humans (the level's objects): `shapes`, posed now.
    /// Humans::setStrikeContact() gives it.
    using StrikeContact = std::function<void(Human& human, std::span<const PosedShape> shapes)>;
    /// The strike test, after every human's move (human::Humans): while any strike shape is on, its posed shapes
    /// meet each of `victims` (the humans it fights) whose spine or head they overlap, each once while the shapes stay
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
    [[nodiscard]] TargetState state() const override;
    /// Its health (record `+0x144`).
    [[nodiscard]] const combat::Health& health() const override { return m_fighter.health(); }
    [[nodiscard]] anim::Vec3 velocity() const override { return m_velocity; }
    /// The horizontal speed.
    [[nodiscard]] float speed() const;
    /// The gait of the whole velocity's length (`+0x1a8`, written with the velocity: a jump's vertical speed counts).
    [[nodiscard]] Gait gait() const;
    [[nodiscard]] bool airborne() const { return m_airborne; }
    /// The human fell more than kOutOfWorldDepth below the mesh; it stays put until spawned again.
    [[nodiscard]] bool outOfWorld() const { return m_outOfWorld; }
    /// With a skeleton and in the world, its spine and head shapes are posed each update for the strike test.
    [[nodiscard]] bool struckByShapes() const override { return m_skeleton != nullptr && !m_outOfWorld; }
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
    [[nodiscard]] const HumanAnimator& animator() const { return m_animator; }
    /// `HuUseAnim`'s idle replacement, `name` loaded as `clip` (null for none: the own idle);
    /// HumanAnimator::setIdleClip(). The play mode applies ScriptState's override for the idle (`0x184`) through it.
    void setIdleClip(std::string_view name, const anim::AnimClip* clip) {
        m_idleClipName = std::string(name);
        m_animator.setIdleClip(clip);
    }
    /// A scripted clip played from outside the dispatcher (HumanAnimator::playScripted()).
    void playScripted(const anim::AnimClip& clip, std::uint32_t animId, float rate, float fade, HeldFlags held) {
        m_animator.playScripted(clip, animId, rate, fade, held);
    }
    /// The name of the idle replacement applied (empty for none).
    [[nodiscard]] const std::string& idleClipName() const { return m_idleClipName; }
    /// `HuUseAnyAnim`'s replacement for anim `id` other than the idle, `name` loaded as `clip` (null: the own clip
    /// plays); an empty `name` takes it out. HumanAnimator::setOverride().
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
    [[nodiscard]] std::vector<HumanSound> takeSounds();
    /// Starts (or stops) keeping its sounds for takeSounds().
    void reportSounds(bool on);
    /// Adds a sound the level found for this human, such as his strike on the level, an object or a car. It is kept
    /// for takeSounds() only while he reports sounds.
    void reportSound(const HumanSound& sound);
    /// The material of the ground under the feet (`+0x1d8`): the triangle the last ground snap hit, `CONCRETE` (5)
    /// before any. **Coney's reading**: the snap's triangle (docs/research/sound.md#anim-sounds names `+0x1d8` the
    /// ground under the human).
    [[nodiscard]] std::uint8_t groundMaterial() const { return m_groundMaterial; }
    /// Whether the last ground snap stood him on a triangle with flag bit 5 (`0x20`), the covered ground that decides
    /// which ambient emitters a player hears (`+0x5b7`, docs/research/sound-events.md#covered).
    /// @orig 0x002195e0 Human_SetCoverFlag5 (unknown)
    [[nodiscard]] bool onCoveredGround() const { return m_coveredGround; }
    [[nodiscard]] const Speeds& speeds() const { return m_animator.speeds(); }
    /// Stamina (record `+0x14a`).
    [[nodiscard]] const Stamina& stamina() const { return m_stamina; }
    /// Whether a sprint is asked for (state flag `0x1000000`): L2 held with stamina left.
    [[nodiscard]] bool sprinting() const { return m_sprinting; }
    /// The body's lean, radians (`+0x29c`), positive leaning left.
    [[nodiscard]] float lean() const { return m_lean; }
    /// What the human is doing beyond walking.
    [[nodiscard]] Traversal traversal() const;
    /// What the locomotion gate reads of the human now: the record's `+0x08` and state code, airborne, held.
    [[nodiscard]] GateInput gateInput() const;
    /// Whether the stick does not move the human now: combat's states hold the body (Fighter::holdsMovement()), it is
    /// busy (stickBusy()), the stick's velocity is gated (stickVelocityGated()) or it is arrested.
    [[nodiscard]] bool stickHeld() const;
    /// The climb in progress, if any.
    [[nodiscard]] const std::optional<ClimbProbe>& climb() const { return m_climbProbe; }
    /// The body's scale (`+0x65c`).
    [[nodiscard]] float scale() const { return m_scale; }
    [[nodiscard]] float bodyScale() const override { return m_scale; }
    /// The radius of its collision capsule (the shape's `+0x40`, 0.35 m, × its scale): human::walkingRadius().
    [[nodiscard]] float capsuleRadius() const { return walkingRadius(m_scale); }
    /// `HuSetScale`: the body's scale, not range-checked. Coney's body (its capsule, its walls' radius, its root
    /// motion) is sized from the scale at each use, so the body follows at once.
    /// @orig 0x0023b0e0 Human_SetScale (unknown)
    void setScale(float scale) { m_scale = scale; }
    /// The fighting: combat's state, meters and what it last did.
    [[nodiscard]] const Fighter& fighter() const { return m_fighter; }
    [[nodiscard]] Fighter& fighter() { return m_fighter; }
    /// The Anim Range List its hits take their damage from (its own copy when made with a class damage table); null
    /// for none.
    [[nodiscard]] const combat::AnimRangeList* ranges() const { return m_ranges; }
    /// `HuApplyDamageModifier(human, factor)`: its attack damages become its class's values × `factor`, rounded (then
    /// a player's Warrior class percentage, as when it was made); each call starts from the class values, and an entry
    /// that rounds to 0 keeps its damage. Nothing for a human made without a class damage table.
    /// @orig 0x00229b90 Human_ScaleAttackDamages (unknown)
    void applyDamageModifier(float factor);

    // Being attacked: the entry points another human and the tests use. Each is acted on at the human's next step
    // (docs/research/combat.md#being-hit-runtime).

    /// A hit on the human (Fighter::takeHit()): the update keeps its largest.
    void takeHit(const IncomingHit& hit) { m_fighter.takeHit(hit); }
    /// Combatant's name for takeHit().
    void hit(const IncomingHit& hit) override { m_fighter.takeHit(hit); }
    /// An attacker's clip warns the human of its hit (Fighter::warn()): a blocking human ducks or blocks early.
    void warn(const AttackNotice& notice) override { m_fighter.warn(notice); }
    /// Keeps where an attack started near it (event `0x10`) for its brain, which counts them (brain `+0x200`).
    void announceAttack(anim::Vec3 attacker) override { m_announced.push_back(attacker); }
    /// Where the attacks announced since the last call started, and forgets them (its brain takes them each update).
    [[nodiscard]] std::vector<anim::Vec3> takeAttackAnnouncements() { return std::exchange(m_announced, {}); }
    /// The slow-motion event (type `0x2e` on, `0x2f` off) the clip playing passed in the last update, if any; the
    /// later of the two when both did (docs/research/camera.md#slow-motion).
    [[nodiscard]] std::optional<std::uint16_t> slowMotionEvent() const { return m_slowMotionEvent; }
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
    void setAttached(bool attached) override { m_fighter.setHoldAttached(attached); }
    [[nodiscard]] bool attached() const override { return m_fighter.holdAttached(); }
    void keepHold() override { m_fighter.keepHold(); }
    [[nodiscard]] std::optional<TargetState> takeBrokenHold() override { return m_fighter.takeBrokenHold(); }
    [[nodiscard]] std::optional<CounterPress> takeCounterPress() override { return m_fighter.takeCounterPress(); }
    /// Moves it there and stops it (its velocity goes).
    void place(anim::Vec3 position, float headingRadians) override;
    /// An AI's jump (`Human_BeginJump` with argument 1, then `Human_LaunchJump`, docs/research/ai.md#route-jump): from
    /// any gait, standing too, launched at `velocity` with the jump's clip. Refused (false) in the air, while climbing,
    /// or while the record `+0x08` holds any of kAiJumpRefuseFlags.
    /// @orig 0x0023db48 Human_BeginJump (unknown)
    /// @orig 0x002217f0 Human_LaunchJump (unknown)
    bool launchJump(anim::Vec3 velocity);
    void face(anim::Vec3 point) override;
    [[nodiscard]] const characters::AnimSet& anims() const override { return m_animator.anims(); }

    // The scripts' hold on the human (docs/references/bindings/character.md): its flags and state, and what they do to
    // it. Each acts at once; the flags and the state keep across spawn().

    /// The flag word (human `+0xe0`, human/human_flags.h), kept by its fighter.
    [[nodiscard]] std::uint64_t flags() const { return m_fighter.flags(); }
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
    /// `HuSetHealthPercent`: health becomes `percent` of the maximum, truncated to whole points; a value outside
    /// (0, 100] gives full health.
    /// @orig 0x002378a8 Human_SetHealthPercent (unknown)
    void setHealthPercent(float percent);
    /// `HuRevive` (Fighter::revive()).
    void revive() { m_fighter.revive(m_animator); }
    /// `HuSetNormalMode` (Fighter::setNormal()); an arrest ends too.
    void setNormalMode(bool full);
    /// Breaks any pair it is in from outside (Fighter::breakPair()), as a placement does.
    void breakPair() { m_fighter.breakPair(); }
    /// `HuSetArrested`: arrested, the human stops where it is, its fighting ends and it loops 320
    /// `ANIM_ARRESTED_IDLE`; released, it stands again with the idle (docs/research/crimes.md#arrest).
    /// @orig 0x00237700 Human_SetArrested (unknown)
    void setArrested(bool arrested);
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

    // Triangle's context action and the pick-up (docs/research/crimes.md#triangle, docs/research/combat.md#breakables).

    /// What triangle tries after a climb (step 4 and 5 of `Player_TriangleAction`): the level's search for a context
    /// record or a loose object, which starts what it found on the human (or drops what he holds) and returns true.
    /// None: nothing.
    using ContextAction = std::function<bool(Human& human)>;
    void setContextAction(ContextAction action) { m_contextAction = std::move(action); }
    /// What triangle tries before the level's context action: the kind-0 record, a cuffed human to free, comes first
    /// in the kinds' order (docs/research/crimes.md#triangle). None: nothing.
    void setFirstContextAction(ContextAction action) { m_firstContextAction = std::move(action); }
    /// What an airborne body touches before the walls push it (`Human_OnContact`'s object branch and the jump's strike
    /// shapes, docs/research/objects.md#pane-break): the level breaks the panes whose bodies a sphere at `centre` of
    /// `radius` reaches. Humans::setBodyContact() gives it; null: nothing.
    using BodyContact = std::function<void(Human& human, anim::Vec3 centre, float radius)>;
    void setBodyContact(const BodyContact* contact) { m_bodyContact = contact; }
    /// What a walking body slides along beyond the level's walls: the world objects' `BLOCKHUMANS` bodies
    /// (`Human_OnContact` slides along an object as along the level, docs/research/physics.md#contacts). Given the
    /// walking sphere's `centre`, `radius` and the update's `move`, the horizontal push out of the body it is deepest
    /// in, or nothing; `walker` is the human, whose gait decides what else the contact does (a sprinter strikes a
    /// `RUNTARGET` body). Humans::setObjectPush() gives it; null: nothing.
    using ObjectPush =
        std::function<std::optional<anim::Vec3>(const Human& walker, anim::Vec3 centre, float radius, anim::Vec3 move)>;
    void setObjectPush(const ObjectPush* push) { m_objectPush = push; }
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
    /// with `stageTurns` turns of the stick a stage (combat::stereoStageTurns()). **Coney's reading**: the turn is not
    /// spread over the intro.
    void startStereoTheft(anim::Vec3 point, float stageTurns);
    /// Enters the step control for one step toward `heading` (radians) with the turn boost `boost`: the next state
    /// update that finds the human free plays the step's clip (stepClipFor(); the fight stance's while locked onto a
    /// target), turns him to its end facing over half the clip, and leaves the control. Until then he waits, not
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
    [[nodiscard]] bool stereoTheftPlaying() const;
    /// Starts freeing a cuffed human at `cuffed` by the mash (`Uncuff_Start`, docs/research/crimes.md#uncuffing): he
    /// turns to him over 325 `ANIM_ARREST_RELEASE_INTRO_FRONT`, which holds `0x2000000` and so keeps the mash's input
    /// closed, then loops 329; the mash (mode 1) runs with the Warrior factor `mashFactor` (combat::mashFactor()).
    /// @orig 0x00260ca8 Uncuff_Start (unknown)
    /// @orig 0x0022d3f8 Uncuff_BeginMash (unknown)
    void startUncuff(anim::Vec3 cuffed, float mashFactor);
    /// Whether the freer's clips (325 or 329) still play: false once a hit or anything else took the body.
    [[nodiscard]] bool uncuffPlaying() const;
    /// The freer's end: the mash stops and he plays 332 `ANIM_ARREST_RELEASE_END`, or 331
    /// `ANIM_ARREST_RELEASE_HIT_REACT` after a hit, then his idle.
    void endUncuff(bool hit);
    /// The cuffed human's half while `freer` frees him: 326 paired to the freer, then the loop 330.
    void playUncuffReact(const Human& freer);
    /// The cuffed human's end: freed, 333 `ANIM_ARREST_RELEASE_END_REACT` paired to `freer` then his idle; still
    /// cuffed, back to 320.
    void endUncuffReact(const Human& freer, bool freed);
    /// Turns to face `point` over `seconds` of updates, whatever plays (`Human_TurnToFacePoint`); the human stays where
    /// he stands. A Warrior handed a swap turns to the presser this way.
    /// @orig 0x00221c20 Human_TurnToFacePoint (unknown)
    void turnToFace(anim::Vec3 point, float seconds);
    /// Starts a tag's spray clips (`Tag_StartSprayClips`): 334 `ANIM_TAGGING_INTRO`, then the loop 335, turning to face
    /// the tag at `point` over half of 334's length (`Human_TurnToFacePoint`); the human stays where he stands.
    /// Returns false when the intro clip is not loaded. docs/research/crimes.md#tag-callbacks
    /// @orig 0x00278018 Tag_StartSprayClips (unknown)
    bool startTagSpray(anim::Vec3 point);
    /// Whether the spray's intro is over and its loop plays: the moment the stick game goes live (the intro's end hook
    /// `0x00277ed8`).
    [[nodiscard]] bool tagSprayLooping() const;
    /// Whether the spray's clips still play (the intro or the loop): false once something else took the body.
    [[nodiscard]] bool tagSprayPlaying() const;
    /// The spray is over: a spray clip still playing leaves for the idle. **Coney's reading**: what the original
    /// plays at Tag_End is not on the page.
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
    void locomote(bool gated);
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
    void moveInAir(const raycast::CollisionMesh& mesh);
    // Keeps the move the walls left (`slid`, against the asked `displacement` at slope `factor`) as the velocity, so a
    // wall met at a steep angle brakes the human (docs/research/characters.md#walls).
    void keepSlidVelocity(anim::Vec3 slid, anim::Vec3 displacement, float factor);
    // Where a walking sweep ends: the feet, and the part of the move from them that the corner slide made.
    struct Swept {
        anim::Vec3 feet;
        anim::Vec3 slide{};
    };
    // Sweeps the walking body from the feet at `from` by `displacement`, sliding off walls and round a corner its walls
    // name; returns where it ends, or nothing when it is still blocked after three passes.
    // @orig 0x0033e278 PhysicsBody_Sweep (unknown)
    [[nodiscard]] std::optional<Swept> sweep(const raycast::CollisionMesh& mesh, anim::Vec3 from,
                                             anim::Vec3 displacement);
    // Pushes the airborne body (a player's larger sphere) out of the walls it overlaps after moving to `feet`.
    // @orig 0x0021a490 Human_PushOutInAir (unknown)
    [[nodiscard]] std::optional<anim::Vec3> pushOutInAir(const raycast::CollisionMesh& mesh, anim::Vec3 feet);
    // The ground snap from the feet at `feet`: on a hit the feet go onto it; a miss starts a fall.
    // @orig 0x0023eab8 Human_SnapToGround (unknown)
    void snapToGround(const raycast::CollisionMesh& mesh, anim::Vec3 feet);
    // Lands: clears the airborne state and the vertical speed; a jump's landing plays its clip.
    // @orig 0x0023e090 Human_Land (unknown)
    void land(anim::Vec3 feet);
    // The materials the body and the snap pass through now: the fences while climbing over.
    [[nodiscard]] std::span<const std::uint8_t> passThrough() const;

    // The stick step is skipped (combat's states, or the human is busy): the clip and an attack's slide move the body;
    // a block turns toward the stick in place (the shuffle); a standing grab turns and walks the pair by the stick.
    void holdForCombat();
    // Locked onto `target`: faces it and walks at the combat walk's speed along the stick without turning, in the
    // combat-walk clip of the stick's angle from the facing; with the velocity `gated`, it only faces the target and
    // the clip playing goes on.
    // @orig 0x00241b90 Human_FightStanceMove (unknown)
    void combatWalk(const Combatant& target, bool gated);
    // Combat's update from the record: the stick turned into the facing frame, the game time, the targets.
    void fight(std::span<Combatant* const> targets);
    // The attacker's side of a warning: an event 0x24 or 0x26 of the clip playing that this step's animation passed
    // (since `beforeTime` of `before`, which played `beforeId`, when it is still the clip) tells the fighter's target,
    // when it stands within twice the anim's reach.
    // @orig 0x00101dd8 Anim_FireEvents (unknown)
    void sendWarnings(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // Notes a slow-motion event (0x2e or 0x2f) of the clip playing that this step's animation passed, as
    // sendWarnings() finds a warning.
    // @orig 0x00101dd8 Anim_FireEvents (unknown)
    void noteSlowMotion(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // Switches the strike shapes by the events of the clip playing that this step's animation passed (as the held
    // flags' events fire, anim::eventFrame()). **Coney choice**: when another clip takes the top before the one that
    // switched them on ends its window, they go off, as its own off event would have switched them.
    // @orig 0x00101dd8 Anim_FireEvents (unknown)
    void noteStrikeEvents(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // Notes the release event (type 10) of the clip playing when this step's animation passed it, as
    // noteStrikeEvents() does; takeThrowRelease() reads it.
    void noteReleaseEvent(const anim::AnimTask* before, std::uint32_t beforeId, float beforeTime);
    // The world placement of the body the strike shapes are posed on.
    [[nodiscard]] BodyPlacement placement() const;
    // Stamina's drain and refill, then the sprint flag, for this update's L2.
    void updateMeters(bool sprintHeld);
    // The hidden state's own ends each update: a sprint, the fight stance or the grace running out
    // (docs/research/stealth.md#hidden).
    void updateHiding();
    // Triangle: a climb (stick above the dead zone), then the context action, then a jump.
    // @orig 0x0027c120 Player_UpdateActions (unknown)
    void tryActions(const raycast::CollisionMesh* mesh, bool sprintHeld);
    // The context action triangle tries after a climb: the level's (setContextAction()), which also drops what is in
    // hand when there is nothing to take. Returns whether it did something.
    [[nodiscard]] bool tryContextAction();
    // A pick-up's step after the animation's: the turn, and the take at the clip's event; a clip replaced ends it.
    void followPickUp();
    // Tries a climb toward `direction`; starts it and returns true on success.
    [[nodiscard]] bool tryClimb(const raycast::CollisionMesh& mesh, anim::Vec3 direction);
    // Tries a jump; launches it and returns true on success.
    [[nodiscard]] bool tryJump(const raycast::CollisionMesh* mesh, anim::Vec3 direction);
    // A climb's work as its clips change: the probe and move at the second clip, the end.
    // @orig 0x00281450 Climb_RunningClipEnd (unknown)
    // @orig 0x00281838 Climb_StandingClipEnd (unknown)
    void followClimb(const raycast::CollisionMesh* mesh);
    // Ends a climb.
    void endClimb();

    PlayerRecord m_record;
    ScriptState m_script;
    HumanAnimator m_animator;
    std::string m_idleClipName;                           // setIdleClip()
    std::map<std::uint32_t, std::string> m_overrideNames; // setOverrideClip()
    std::unique_ptr<combat::AnimRangeList> m_ownRanges;   // the list with the class's damage, when it has one
    std::vector<std::int16_t> m_classDamage;              // the class's damage table, for HuApplyDamageModifier
    int m_damagePercent = 0;                              // a player's Warrior class percentage (0: not a player)
    const combat::AnimRangeList* m_ranges;
    FighterProfile m_profile;
    Fighter m_fighter;
    std::vector<anim::Vec3> m_announced;            // attacks announced since the brain last looked (event 0x10)
    std::optional<std::uint16_t> m_slowMotionEvent; // the last update's slow-motion event (0x2e / 0x2f)
    float m_stepSeconds = kStepSeconds;             // the characters' step (slow motion shortens it)
    std::uint64_t m_updates = 0;                    // updates stepped: combat's game time
    std::array<anim::Quat, anim::kPoseBones> m_bindRotations{};
    float m_scale = 1.0F;
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
    std::uint16_t m_groundFlags = 0;               // the last ground snap's triangle flags
    bool m_hidden = false;                         // state 0x200000
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
    // Starts a turn to `point` spread over `share` of `clip`'s playing time; nothing when the clip is not loaded.
    void turnOverClip(std::uint32_t clip, anim::Vec3 point, float share);
    std::optional<double> m_pickedUp; // the object a pick-up reached, until takePickedUp()
    bool m_throwRelease = false;      // a release event passed, until takeThrowRelease()
    ContextAction m_contextAction;
    ContextAction m_firstContextAction;         // tried before m_contextAction (a cuffed human to free)
    const BodyContact* m_bodyContact = nullptr; // setBodyContact(), the step's
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
    std::vector<std::uint16_t> m_nearby; // scratch for the wall test
};

} // namespace coney::human
