// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/stick.h"
#include "human/locomotion.h"

// The player's attacks: which one square or cross starts, the moving attacks, and the chain that turns a second and
// third press into a three-hit combo, with each attack's wind-up, hit, chain window and recovery counted in updates.
// Research: docs/research/combat.md#attacks, docs/research/combat.md#run-attacks, docs/research/combat.md#breakables

namespace coney::combat {

/// The attack phase bits of the record's `+0x08`, as the original sets them while an attack plays.
inline constexpr std::uint32_t kPhaseWindUp = 0x1;
inline constexpr std::uint32_t kPhaseChainWindow = 0x2;
inline constexpr std::uint32_t kPhaseEnd = 0x4;
inline constexpr std::uint32_t kPhaseGrabStart = 0x10; ///< Set at a grab or tackle start and in object attacks.
inline constexpr std::uint32_t kPhaseTackling = 0x200;
inline constexpr std::uint32_t kPhaseRecovery = 0x40000;

/// The stick length beyond which square snaps or attacks from a run.
inline constexpr float kSnapStick = 0.95F;
/// The stick length from which square attacks from a walk.
inline constexpr float kWalkAttackStick = 0.12F;
/// An object point up to this height above the feet takes the low break clip; above it the mid one.
inline constexpr float kObjectLowHeight = 0.8F;

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
};

/// The attack square starts: 193 at a grounded target, 212 at a mounted one, 120 at a grabbed one; an object attack
/// at a breakable (anim_id::kNone here: objectAttack() picks the clip); with the stick beyond 0.95 more than 45° off
/// the facing a snap (25 right, 27 left, 29 back); at a run with no phase bit and the stick beyond 0.95 the run
/// attack 24; walking (gait 1-3) with the stick at 0.12 or more the walk attack 23; otherwise `S1`.
/// **Coney choice**: at a sprint (gait 5) square is `S1`, as the research lists no sprint case; a grounded target
/// takes 193, never 194.
/// @orig 0x00286cc8 Player_Square (unknown)
[[nodiscard]] int squareAttack(const SquareInput& input);

/// The attack cross (its 0x10) starts: `X1`. (A held weapon of types 4, 5 or 6 takes another routine; Coney has no
/// weapons yet.)
/// @orig 0x00287a18 Player_Cross (unknown)
[[nodiscard]] int crossAttack();

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

/// What one update of an attack did.
struct ChainStep {
    int started = anim_id::kNone; ///< An attack started this update (the next of the chain).
    int hit = anim_id::kNone;     ///< The attack whose hit landed this update.
    bool finished = false;        ///< The attack ended with nothing after it; the fight idle returns.
};

/// One attack playing and the chain after it, counted in updates.
///
/// The timing is CombatTuning's: the hit 2 updates after the start, the chain window from 6 to 15, the end phase,
/// the recovery from 17 and the end at 20. A press is buffered (one at a time, a later one replacing it) during the
/// chain window, or during the wind-up while the combo count is below 2 (or is 2 on `SS2`); a buffered press plays
/// its attack as soon as the window is open. Presses in the end phase and the recovery are dropped.
class AttackChain {
  public:
    /// Starts attack `animId` from its first update, adding one to the combo count.
    /// @orig 0x002625a8 Attack_Start (unknown)
    void start(int animId);
    /// Stops the attack and forgets the chain (a hit taken, a grab).
    void cancel();

    /// Advances one update with this update's chain press.
    /// @orig 0x00280630 Player_UpdateChain (unknown)
    /// @orig 0x00280708 Player_UpdateChain (unknown)
    ChainStep update(ChainButton press, const CombatTuning& tuning);

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
    /// The phase bits of the record's `+0x08` for the attack's age; 0 when no attack plays.
    [[nodiscard]] std::uint32_t phaseFlags(const CombatTuning& tuning) const;

  private:
    // Whether a press is buffered in the attack's current phase.
    [[nodiscard]] bool accepts(const CombatTuning& tuning) const;

    int m_current = anim_id::kNone;
    int m_age = 0;
    int m_combo = 0;
    ChainButton m_buffered = ChainButton::None;
};

} // namespace coney::combat
