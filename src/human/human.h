// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/anim_set.h"
#include "combat/anim_ranges.h"
#include "combat/commands.h"
#include "human/climb.h"
#include "human/combatant.h"
#include "human/fighter.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"
#include "human/script_state.h"
#include "human/stamina.h"
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
    combat::CommandId command = combat::command::kNone; ///< The command (`+0x20`).
    std::uint16_t buttons = 0;                          ///< The held buttons.
    /// The brain's move, which replaces the stick while set. **Coney choice**: kept beside the record, as Coney's
    /// locomotion reads only the record; the original keeps it in the brain.
    std::optional<BrainMove> move;
};

/// The record part of `input`.
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
/// driven by its record. Another human fights it as a Combatant.
class Human final : public Combatant {
  public:
    /// Gravity while airborne, m/s² (1.6 g), and the fastest fall, m/s.
    static constexpr float kGravity = 15.68F;
    static constexpr float kMaxFallSpeed = 50.0F;
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

    /// The update's first pass, the animation step: every task advances (the clips' events moving the record's
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
    /// The ground normal from the last snap.
    [[nodiscard]] anim::Vec3 groundNormal() const { return m_groundNormal; }
    /// The vertical speed at the last landing (negative), 0 before any.
    [[nodiscard]] float lastLandingSpeed() const { return m_lastLandingSpeed; }
    /// The last stick intent, after the camera's turn.
    [[nodiscard]] const StickIntent& intent() const { return m_intent; }
    [[nodiscard]] const HumanAnimator& animator() const { return m_animator; }
    /// Called with each anim id the human starts playing, for the scripts' animation callbacks
    /// (docs/research/characters.md#anim-callbacks); empty for none.
    void setAnimStartHook(std::function<void(std::uint32_t animId)> hook) { m_animator.setStartHook(std::move(hook)); }
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
    /// The fighting: combat's state, meters and what it last did.
    [[nodiscard]] const Fighter& fighter() const { return m_fighter; }
    [[nodiscard]] Fighter& fighter() { return m_fighter; }
    /// The Anim Range List its hits take their damage from (its own copy when made with a class damage table); null
    /// for none.
    [[nodiscard]] const combat::AnimRangeList* ranges() const { return m_ranges; }

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
    [[nodiscard]] bool alive() const { return !m_fighter.health().depleted() && !m_script.arrested; }
    /// Health as a percentage of its maximum, 0-100 (`HuGetHealthPercent`).
    /// @orig 0x00237c38 Human_GetHealthPercent (unknown)
    [[nodiscard]] float healthPercent() const { return m_fighter.health().fraction() * 100.0F; }
    /// `HuSetHealthPercent`: health becomes `percent` of the maximum, truncated to whole points; a value outside
    /// (0, 100] gives full health.
    /// @orig 0x002378a8 Human_SetHealthPercent (unknown)
    void setHealthPercent(float percent);
    /// `HuRevive` (Fighter::revive()).
    void revive() { m_fighter.revive(m_animator); }
    /// `HuSetNormalMode` (Fighter::setNormal()); an arrest ends too.
    void setNormalMode(bool full);
    /// `HuSetArrested`: arrested, the human stops where it is and its fighting ends; released, it stands again with
    /// the idle.
    /// @orig 0x00237700 Human_SetArrested (unknown)
    void setArrested(bool arrested);
    /// `HuSetFullRage`: the rage meter full, held for `holdMs` before it decays.
    void fillRage(int holdMs) { m_fighter.combat().rage().fill(nowMs(), holdMs); }
    /// `HuSetRageFrac`: the rage meter at `fraction` of its maximum.
    void setRageFraction(float fraction) { m_fighter.combat().rage().setFraction(fraction, nowMs()); }
    /// `HuSetLockedRage`: flag::kRageLocked, and the meter locked or unlocked at once.
    void setRageLocked(bool locked);

    /// Another human's grab catches this one (Fighter::catchInGrab()).
    void catchInGrab(const GrabCatch& grab) { m_fighter.catchInGrab(grab); }
    /// The grabber's numbers this update, while held (Fighter::updateGrabber()).
    void updateGrabber(const combat::GrabberState& grabber) { m_fighter.updateGrabber(grabber); }
    /// The grabber lets go (Fighter::releaseFromGrab()).
    void releaseFromGrab() { m_fighter.releaseFromGrab(m_animator); }

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
    // Sweeps the walking body from the feet at `from` by `displacement`, sliding off walls; returns where it ends, or
    // nothing when it is still blocked after three passes.
    // @orig 0x0033e278 PhysicsBody_Sweep (unknown)
    [[nodiscard]] std::optional<anim::Vec3> sweep(const raycast::CollisionMesh& mesh, anim::Vec3 from,
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
    // Stamina's drain and refill, then the sprint flag, for this update's L2.
    void updateMeters(bool sprintHeld);
    // Triangle: a climb (stick above the dead zone), then the context action, then a jump.
    // @orig 0x0027c120 Player_UpdateActions (unknown)
    void tryActions(const raycast::CollisionMesh* mesh, bool sprintHeld);
    // The context action triangle tries after a climb; a no-op hook (doors, pick-ups: not researched). Returns whether
    // it did something.
    [[nodiscard]] bool tryContextAction();
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
    std::unique_ptr<combat::AnimRangeList> m_ownRanges; // the list with the class's damage, when it has one
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
    float m_lastLandingSpeed = 0.0F;
    Stamina m_stamina;
    bool m_sprinting = false;
    bool m_jumping = false;
    float m_lean = 0.0F;
    float m_lastTurn = 0.0F; // the heading's change in the last state update, for the lean
    std::optional<ClimbRun> m_climbRun;
    std::optional<ClimbProbe> m_climbProbe;
    std::vector<std::uint16_t> m_nearby; // scratch for the wall test
};

} // namespace coney::human
