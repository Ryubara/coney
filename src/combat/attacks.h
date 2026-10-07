// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation/anim_task.h"
#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/stick.h"
#include "human/locomotion.h"

// The player's attacks: which one square or cross starts, the moving attacks, and the chain that turns a second and
// third press into a three-hit combo. An attack's phases (wind-up, chain window, end, recovery) are the bits of the
// record's `+0x08` its clip holds and its events move (docs/research/tasks.md#held-flags); the chain reads them. Only
// the hit is counted in updates.
// Research: docs/research/combat.md#attacks, docs/research/combat.md#run-attacks, docs/research/combat.md#breakables

namespace coney::combat {

/// The attack phase bits of the record's `+0x08`, as an attack's clip holds them (docs/research/tasks.md#held-flags).
inline constexpr std::uint32_t kPhaseWindUp = anim::kFlagWindUp;
inline constexpr std::uint32_t kPhaseChainWindow = anim::kFlagChainWindow;
inline constexpr std::uint32_t kPhaseEnd = anim::kFlagAttackEnd;
inline constexpr std::uint32_t kPhaseGrabStart = 0x10; ///< Held by a grab's or tackle's clips, and object attacks.
inline constexpr std::uint32_t kPhaseTackling = 0x200;
inline constexpr std::uint32_t kPhaseRecovery = anim::kFlagRecovery;
inline constexpr std::uint32_t kPhaseDuck = 0x1000;                ///< Ducking under an attack (616).
inline constexpr std::uint32_t kPhaseCounter = 0x2000;             ///< The duck's counter (617-620).
inline constexpr std::uint32_t kPhaseRunAttack = 0x1000000;        ///< The run attack (24), for its whole length.
inline constexpr std::uint32_t kPhaseNormalFromFight = 0x40000000; ///< 389, which nothing refuses.
/// The bits an attack's clip holds while the attack is under way: its phases and recovery, the counter's and the
/// moving attacks'. The chain ends once none is left (the clip has given them back).
inline constexpr std::uint32_t kAttackUnderWay =
    anim::kFlagAttackPhases | kPhaseRecovery | kPhaseCounter | kPhaseRunAttack;

/// The `+0x08` bits that make the dispatcher (`0x0027c120`) return before the chain and every command: the recovery
/// and the run attack's bit among them, not the wind-up, window, end phase, grab bit or 389's
/// (docs/research/combat.md#input-return).
inline constexpr std::uint32_t kDispatchDroppingPhases = 0x05c7fee0;
/// The `+0x08` bits that refuse a new square or cross attack (`0x00286cc8`, `0x00287a18`): the attack phases, the grab
/// bit, the duck and the run attack.
inline constexpr std::uint32_t kAttackRefusingPhases = 0x0100101f;
/// The `+0x08` bits that refuse a special: cross + square's 653 and circle + cross's strong grapple (`Player_Special`,
/// `0x00287730`, docs/research/combat.md#strong-grapple).
inline constexpr std::uint32_t kSpecialRefusingPhases = 0xaeebf7ff;

/// The stick length beyond which square snaps or attacks from a run.
inline constexpr float kSnapStick = 0.95F;
/// The stick length from which square attacks from a walk.
inline constexpr float kWalkAttackStick = 0.12F;
/// An object point up to this height above the feet takes the low break clip; above it the mid one.
inline constexpr float kObjectLowHeight = 0.8F;
/// The snap's target search (`0x0027aa38(h, 0x80)`, docs/research/combat.md#attacks): the nearest human within this
/// many metres (`0x00227598`), ...
inline constexpr float kSnapSearchRange = 2.0F;
/// ... within this angle of the stick's direction (radians, π/4 written to `0x0051096c`) ...
inline constexpr float kSnapSearchCone = 0.7853982F;
/// ... and at most this far above or below (metres, `0x00510970`).
inline constexpr float kSnapSearchHeight = 2.0F;
/// A snap turns the player onto its target over this long (seconds, `Attack_SteerToTarget` from `0x00264460`).
inline constexpr float kSnapSteerSeconds = 0.1F;

/// What square is aimed at: the target's state decides the attack before the stick does.
enum class TargetKind : std::uint8_t {
    None,      ///< No target, or one standing.
    Grounded,  ///< On the ground (196 `GROUNDED_IDLE`).
    Mounted,   ///< Tackled and mounted by the player.
    Grabbed,   ///< Held in a grab.
    Breakable, ///< A breakable object (glass, a car window).
};

/// What square needs to know.
struct SquareInput {
    TargetKind target = TargetKind::None;
    Stick stick; ///< In the player's frame.
    human::Gait gait = human::Gait::Standing;
    std::uint32_t phaseFlags = 0; ///< The record's `+0x08`.
    bool snapAttacks = true;      ///< CombatTuning::snapAttacks.
    /// The snap's search found a human on the stick's side that is not the current target (the caller searches:
    /// kSnapSearchRange, kSnapSearchCone, kSnapSearchHeight).
    bool snapTarget = false;
};

/// The snap a stick asks for: beyond 0.95 and more than 45° off the facing, 25 right, 27 left, 29 back; otherwise
/// anim_id::kNone. It needs a target as well to play (squareAttack()).
[[nodiscard]] int snapForStick(Stick stick);

/// The attack square starts, in the original's order: 193 at a grounded target, 212 at a mounted one, 120 at a
/// grabbed one; an object attack at a breakable (anim_id::kNone here: objectAttack() picks the clip); at a run (gait 4)
/// with no phase bit and the stick beyond 0.95 the run attack 24; walking (gait 1-3) with the stick at 0.12 or more the
/// walk attack 23; only then, standing or sprinting, the snap of snapForStick() when snaps are on and the search found
/// a target (SquareInput::snapTarget); otherwise `S1`. So a snap comes from a player who has not started to walk.
/// **Coney choice**: a grounded target takes 193, never 194.
/// The snap itself is `0x00264460` (docs/research/combat.md#attacks).
/// @orig 0x00286cc8 Player_Square (unknown)
[[nodiscard]] int squareAttack(const SquareInput& input);

/// The attack cross (its 0x10) starts: `X1`. (A held weapon of types 4, 5 or 6 takes another routine; Coney has no
/// weapons yet.)
/// @orig 0x00287a18 Player_Cross (unknown)
[[nodiscard]] int crossAttack();

/// The clips an anim set puts in the slots square and cross read (docs/research/combat.md#bat): `S1`, `X1`, the
/// grounded strike and the mounting strike. Set 0 is the defaults.
struct AnimSetClips {
    int square = anim_id::kAttackS1;          ///< Slot `0x10`.
    int cross = anim_id::kAttackX1;           ///< Slot `0x11`.
    int mounting = anim_id::kMountingStrike;  ///< Slot `0x12`.
    int grounded = anim_id::kGroundedStrike1; ///< Slot `0x13`.
};

/// The clips of anim set `set`: 1 (45, 47, 50, 49), 2 (39, 41, 44, 43) and 3, a bat's (34, 36, 38, 37); any other
/// set keeps the defaults. **Coney's reading**: of the overrides only `S1`'s, `X1`'s and the two strikes' are
/// applied; the walk and run attacks and the snaps stay as they are (which slots they read is not traced).
/// @orig 0x00253688 Human_ApplyAnimSet (unknown)
[[nodiscard]] AnimSetClips animSetClips(int set);

/// The break clip for an object point `height` metres above the feet: below the feet 194, up to 0.8 m 661 `LOW`,
/// above 662 `MID` (663 `HIGH` is never chosen).
/// @orig 0x00264178 Player_ObjectAttack (unknown)
[[nodiscard]] int objectAttack(float height);

/// Whether L2 + cross (the charge) or L2 + square (the dive) may start: at a run with no phase bit, or at a sprint.
/// **Coney choice**: the dive takes the charge's conditions (the research gives them for the charge).
/// @orig 0x0027d800 Player_Charge (unknown)
/// @orig 0x0027d900 Player_Dive (unknown)
[[nodiscard]] bool runningAttackAllowed(human::Gait gait, std::uint32_t phaseFlags);

/// A press the chain may buffer (the record's `+0xb8`).
enum class ChainButton : std::uint8_t { None, Cross, Square, SnapRight, SnapLeft, SnapBack };

/// What `command` with the stick gives the chain: cross pressed (0x12) is Cross, square pressed (0xf) is Square or,
/// with the stick beyond 0.95 off the front and snaps on, the snap of that side. Anything else is None (so cross's
/// 0x10 on the release is not buffered a second time).
[[nodiscard]] ChainButton chainButton(CommandId command, Stick stick, bool snapAttacks);

/// The chain table: the attack `button` plays after `current`, or anim_id::kNone when the chain ends there. S1 → SS2 or
/// SX2, X1 → XS2 or XX2, SS2 → SSS3 or SSX3; a buffered snap plays wherever a square would continue.
/// **Coney choice**: SS2 then square is always `SSS3` (19), never its `_HOLD` variant 20 (19 every time at runtime).
[[nodiscard]] int nextChainAttack(int current, ChainButton button);

/// The update, counted from its clip's start, at which attack `animId`'s hit lands (0: on the start), as measured at
/// runtime (docs/research/combat.md#attacks): `S1` CombatTuning::hitUpdate (2), `X1` 8, `SS2` 4, `SSS3` and `SSX3` 7,
/// `XX2` 10, `SX2` 7, `XS2` 9, the grab strikes 51, 53 and 55 1, the power strike 57 0. **Coney choice**: every attack
/// not measured hits as `S1` does (its hit event is not mapped).
[[nodiscard]] int attackHitUpdate(int animId, const CombatTuning& tuning);

/// What one update of an attack did.
struct ChainStep {
    int started = anim_id::kNone; ///< An attack started this update (the next of the chain).
    int hit = anim_id::kNone;     ///< The attack whose hit landed this update.
    bool finished = false;        ///< The attack ended with nothing after it; the fight idle returns.
};

/// One attack playing and the chain after it.
///
/// The attack's phases are the record's `+0x08` (`flags`) as its clip holds them (docs/research/tasks.md#held-flags):
/// the wind-up `0x1`, the chain window `0x2` its event `0x2c` opens, the end phase `0x4` from `0x2d` and the recovery
/// `0x40000` from `0x48`, all cleared when the clip ends. A press is buffered (one at a time, a later one replacing it)
/// while the window is open, or in the wind-up while the combo count is below 2 (or is 2 on `SS2`); a buffered press
/// plays its attack as soon as the window is open. Presses in the end phase and the recovery are dropped. The attack
/// is over when its clip has given back its bits (none of kAttackUnderWay is left). Its hit lands attackHitUpdate()
/// updates after its start.
class AttackChain {
  public:
    /// Starts attack `animId` from its first update, adding one to the combo count; the caller plays its clip holding
    /// the attack's bits. Returns whether its hit lands on the start (hit update 0).
    /// @orig 0x002625a8 Attack_Start (unknown)
    bool start(int animId, const CombatTuning& tuning);
    /// Stops the attack and forgets the chain (a hit taken, a grab).
    void cancel();

    /// Advances one update with this update's chain press and the record's `+0x08` as the update starts.
    /// @orig 0x00280630 Player_UpdateChain (unknown)
    /// @orig 0x00280708 Player_UpdateChain (unknown)
    ChainStep update(ChainButton press, std::uint32_t flags, const CombatTuning& tuning);
    /// The update at which the attack playing hits.
    [[nodiscard]] int hitUpdate() const { return m_hit; }

    /// An attack is playing.
    [[nodiscard]] bool active() const { return m_current != anim_id::kNone; }
    /// The attack playing, or anim_id::kNone.
    [[nodiscard]] int animId() const { return m_current; }
    /// Updates since the attack started.
    [[nodiscard]] int age() const { return m_age; }
    /// Attacks started since the chain began.
    [[nodiscard]] int comboCount() const { return m_combo; }
    /// The buffered press.
    [[nodiscard]] ChainButton buffered() const { return m_buffered; }

  private:
    // Whether a press is buffered with the record's +0x08 at `flags`.
    [[nodiscard]] bool accepts(std::uint32_t flags) const;

    int m_hit = 2;
    int m_current = anim_id::kNone;
    int m_age = 0;
    int m_combo = 0;
    ChainButton m_buffered = ChainButton::None;
};

} // namespace coney::combat
