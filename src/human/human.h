// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/anim_set.h"
#include "combat/anim_ranges.h"
#include "combat/commands.h"
#include "human/climb.h"
#include "human/fighter.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/stamina.h"
#include "human/target_human.h"
#include "human/victim.h"
#include "raycast/collision_mesh.h"

// A human driven by a pad: the player. Its locomotion, sprint and stamina, jump and climbs, its fighting (Fighter), its
// standing on and falling from the level's collision mesh, and its animation, stepped at the characters' fixed 30 Hz.
// Platform-neutral and deterministic: no clock, no randomness, so a scripted pad gives the same path on every run (test
// mode). Research: docs/research/characters.md

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
    std::span<TargetHuman* const> targets;              ///< The humans that can be fought (the sandbox's targets).
};

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

/// The pad-driven human: position (the feet), heading, velocity, ground and air state, stamina, and its animation.
class Human {
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

    /// Places the human at `position` (the feet, game axes) facing `headingDegrees` (0 faces +y), snapped to the
    /// ground of `mesh` (may be null: no snap) with a 2.5 m ray from 1 m above; 0.01 above the hit. Stamina is full
    /// again and anything in progress (a jump, a climb) is dropped.
    /// @orig 0x00218008 Human_Init (unknown)
    void spawn(const raycast::CollisionMesh* mesh, anim::Vec3 position, float headingDegrees);

    /// One update of 1/30 s: the stick turned by the camera; the animation's step (and a climb's moves at its clip
    /// changes); the locomotion, the jump's air control or the climb's own motion, with the clip's root motion added;
    /// gravity while airborne; the move against `mesh`'s walls with the ground snap, or the fall and landing; then the
    /// player's part: stamina, the sprint, triangle's climb or jump, the lean and the animation state.
    /// @orig 0x00240e38 Human_PlayerLocomotion (unknown)
    /// @orig 0x0023fea8 Human_StateUpdate (unknown)
    void step(const HumanInput& input, const raycast::CollisionMesh* mesh);

    /// The pose to draw now.
    [[nodiscard]] anim::Pose pose() const { return m_animator.pose(m_bindRotations); }

    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// Radians, 0 facing +y, anticlockwise from above.
    [[nodiscard]] float heading() const { return m_heading; }
    [[nodiscard]] anim::Vec3 velocity() const { return m_velocity; }
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
    [[nodiscard]] const Speeds& speeds() const { return m_animator.speeds(); }
    /// Stamina (record `+0x14a`).
    [[nodiscard]] const Stamina& stamina() const { return m_stamina; }
    /// Whether a sprint is asked for (state flag `0x1000000`): L2 held with stamina left.
    [[nodiscard]] bool sprinting() const { return m_sprinting; }
    /// The body's lean, radians (`+0x29c`), positive leaning left.
    [[nodiscard]] float lean() const { return m_lean; }
    /// What the human is doing beyond walking.
    [[nodiscard]] Traversal traversal() const;
    /// The climb in progress, if any.
    [[nodiscard]] const std::optional<ClimbProbe>& climb() const { return m_climbProbe; }
    /// The body's scale (`+0x65c`).
    [[nodiscard]] float scale() const { return m_scale; }
    /// The fighting: combat's state, meters and what it last did.
    [[nodiscard]] const Fighter& fighter() const { return m_fighter; }
    [[nodiscard]] Fighter& fighter() { return m_fighter; }
    /// The Anim Range List its hits take their damage from (its own copy when made with a class damage table); null
    /// for none.
    [[nodiscard]] const combat::AnimRangeList* ranges() const { return m_ranges; }

    // Being attacked: the entry points another human (a future AI attacker) and the tests use. Each is acted on at the
    // human's next step (docs/research/combat.md#being-hit-runtime).

    /// A hit on the human (Fighter::takeHit()): the update keeps its largest.
    void takeHit(const IncomingHit& hit) { m_fighter.takeHit(hit); }
    /// An attacker's clip warns the human of its hit (Fighter::warn()): a blocking human ducks or blocks early.
    void warn(const AttackNotice& notice) { m_fighter.warn(notice); }
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
        std::uint32_t phase = 0; // 0, 1 or 2: the clip of the chain playing
        int moveUpdates = 0;     // updates left of the move to the start point
        anim::Vec3 start;        // the start point (feet)
        bool over = false;       // record +0x08 bit 0x40: fences do not block the body
    };

    // The locomotion: target speed, skid, turn, acceleration; sets the horizontal velocity.
    void locomote();
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

    // Combat holds the body: no stick movement; a block turns toward the stick in place (the shuffle); a standing grab
    // turns and walks the pair by the stick.
    void holdForCombat();
    // Locked onto `target`: faces it and walks at the combat walk's speed along the stick without turning, in the
    // combat-walk clip of the stick's angle from the facing.
    // @orig 0x00241b90 Human_FightStanceMove (unknown)
    void combatWalk(const TargetHuman& target);
    // Combat's update: the stick turned into the facing frame, the game time, the targets.
    void fight(const HumanInput& input);
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

    HumanAnimator m_animator;
    std::unique_ptr<combat::AnimRangeList> m_ownRanges; // the list with the class's damage, when it has one
    const combat::AnimRangeList* m_ranges;
    Fighter m_fighter;
    std::uint64_t m_updates = 0; // updates stepped: combat's game time
    std::array<anim::Quat, anim::kPoseBones> m_bindRotations{};
    float m_scale = 1.0F;
    anim::Vec3 m_position;
    float m_heading = 0.0F;
    anim::Vec3 m_velocity; // z is the vertical speed (+0x3a0)
    StickIntent m_intent;
    float m_lastMagnitude = 0.0F;
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
    std::optional<ClimbRun> m_climbRun;
    std::optional<ClimbProbe> m_climbProbe;
    std::vector<std::uint16_t> m_nearby; // scratch for the wall test
};

} // namespace coney::human
