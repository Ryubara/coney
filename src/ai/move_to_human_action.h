// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/action.h"

// The move-to-human action (`MoveMelee`): the fight's footwork, which keeps a band of distances from the brain's target
// for at most a time limit. It writes no stick: it sets the brain's move (a heading and a speed, brain `+0x110`,
// `+0x114`), which the human's locomotion follows with the same turns and gaits as the player's stick.
// Research: docs/research/ai-moving.md#move-melee, docs/research/ai-fights.md#fight-reposition

namespace coney::ai {

/// Beyond this distance the plain move runs, closer it walks (**Coney choice**: at runtime a civilian ran from 8 m and
/// walked close by; the boundary is not traced).
inline constexpr float kRunDistance = 4.0F;

/// The band's counters: a radial or side step lasts this many updates (`MoveMeleeAction_Update` `0x002fd4e8`).
inline constexpr int kBandSteps = 10;
/// A radial step with fewer updates left than this counts as settled once the mover is in the band.
inline constexpr int kBandSettledSteps = 6;
/// What the side pick answers: circle toward sector 2 or 6, or go straight (8).
inline constexpr int kSideLeft = 2;
inline constexpr int kSideRight = 6;
inline constexpr int kSideNone = 8;

/// Whether `brain`'s class role (`CharClass` `+0x11b`) is 6 or 7, a class that fights with no stance. **Coney
/// stand-in**: the role is read through the gang's lookups (GangLookups::role), so a brain with no gang never counts.
/// @orig 0x002fd128 Brain_IsKind6Or7 (unknown)
[[nodiscard]] bool fightsWithoutStance(const Brain& brain);

/// The move to the target.
/// @orig 0x002fcf50 MoveToHumanAction_Init (unknown)
class MoveToHumanAction final : public Action {
  public:
    /// A plain move of at most `limitMs` that ends within `stopDistance` of the target. **Coney stand-in**: the
    /// original's callers all keep a band (the other constructor); this one serves the callers not yet moved to it.
    MoveToHumanAction(std::uint32_t limitMs, float stopDistance) : m_limitMs(limitMs), m_far(stopDistance) {}
    /// The band-keeping move: keeps the target between `nearM` and `farM` metres, for at most `limitMs`.
    MoveToHumanAction(std::uint32_t limitMs, float nearM, float farM)
        : m_limitMs(limitMs), m_far(farM), m_near(nearM), m_band(true) {}

    /// Sets the time limit; a band move notes "no stance" (fightsWithoutStance()), leaving the fight stance then
    /// (Brain::setFightStance()), and saves the turn boost.
    /// @orig 0x002fcff0 MoveMeleeAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// A plain move: done (the move stopped) when the target is gone, within the stop distance, or the limit has
    /// passed; otherwise it heads for the target at the run speed beyond kRunDistance and the walk speed within it,
    /// aiming 2 m along the way (Brain::setMoveAim()). **Coney choice**: straight at the target; the path areas and
    /// the steering round other humans (`0x002fc158`, `0x00289138`) are not built.
    ///
    /// A band move: done in a reaction or once the target is not up. Before the limit, with no radial step running,
    /// too near starts kBandSteps updates of backing off and too far of closing in (with a turn boost when closing
    /// within the far melee range on a man facing the same way, on one who is down, or on a moving empty-handed one
    /// while A holds something); the side pick (pickSide()), with no side step running, may start kBandSteps updates
    /// of circling and end the radial one. The move: toward him at the run (the walk with no stance) or away at the
    /// walk, plus the side, at least the walk. Done in the band with no side step and a radial one under
    /// kBandSettledSteps; past the limit, done once both are over. Every class but the "no stance" ones faces the
    /// target; a "no stance" one faces the move. The aim is 2 m along the move, and a human in the fight stance
    /// (Brain::fightStance()) strafes toward it. **Coney stand-ins**: "in a
    /// reaction" is a reaction goal on top; distances and directions are across the ground.
    /// @orig 0x002fd4e8 MoveMeleeAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Stops the move; never refused. A band move restores the turn boost, and a man with no stance turns to a live
    /// target.
    /// @orig 0x002fd068 MoveMeleeAction_Abort (unknown)
    [[nodiscard]] bool abort(Brain& brain) override;

    /// The time limit, ms.
    [[nodiscard]] std::uint32_t limitMs() const { return m_limitMs; }
    /// Whether this is a band move (the three-argument constructor).
    [[nodiscard]] bool band() const { return m_band; }
    /// The band's near distance, m (0 for a plain move).
    [[nodiscard]] float bandNear() const { return m_near; }
    /// The band's far distance, or the plain move's stop distance, m.
    [[nodiscard]] float bandFar() const { return m_far; }
    /// The side counter (`+0x18`): positive circles toward sector 2, negative toward 6.
    [[nodiscard]] int sideSteps() const { return m_side; }
    /// The radial counter (`+0x1c`): positive closes in, negative backs off.
    [[nodiscard]] int radialSteps() const { return m_radial; }

  private:
    // The plain move's update.
    // @coney-only the plain move kept for the callers not yet moved to the band (see the first constructor)
    ActionStatus approach(Brain& brain);
    // The band move's update.
    // @orig 0x002fd4e8 MoveMeleeAction_Update (unknown)
    ActionStatus keepBand(Brain& brain);
    // Which way to circle: when a radial step is blocked ahead (or behind, backing) by someone other than the target,
    // it ends and the freer side is taken; otherwise the cheaper side by the sector costs, and on a tie straight in
    // (8), unless T holds or is held from the rear and is a threat: then round to his back (his front when he is held
    // from the rear), raising the turn boost when A is one of his active attackers. **Coney stand-in**: the tie
    // between two free sides goes by the brain's handle (the original reads bit `0x10000` of the human's handle).
    // @orig 0x002fd158 MoveMeleeAction_PickSide (unknown)
    int pickSide(Brain& brain, const Brain& target);

    std::uint32_t m_limitMs;     // +0x20, made absolute in m_untilMs
    float m_far;                 // +0x14 (squared there); the plain move's stop distance
    float m_near = 0.0F;         // +0x10 (squared there)
    bool m_band = false;         // a band move
    int m_side = 0;              // +0x18
    int m_radial = 0;            // +0x1c
    bool m_noStance = false;     // +0x24
    int m_savedBoost = 0;        // +0x25
    std::uint64_t m_untilMs = 0; // now + the limit, from start()
};

} // namespace coney::ai
