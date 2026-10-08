// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/goal.h"

// The fight reaction goals: what an AI does while it holds a man (grabbing, mounting), is held (grabbed, mounted) or
// is down. Each runs as the brain's reaction goal while its state lasts and ends, its actions cleared, when the state
// clears. They pick with `Human_CanStartAttack` and queue with the kind's chain delay unless said, so an AI's moves in
// a hold go through the same press and dispatcher as the player's.
// Research: docs/research/ai.md#fight-reactions

namespace coney::ai {

class Brain;

/// The direction a grabber throws or pushes his man (`Grabbing_PickMove`'s d): 0 to his right, 1 ahead, 2 to his left,
/// 3 behind him (the game's angle heading + d × 90° − 90° in its clockwise headings).
enum class GrabMove : std::uint8_t { Right = 0, Ahead = 1, Left = 2, Behind = 3 };

/// The stick heading of `move` for a grabber facing `heading` (radians, Coney's convention: anticlockwise from above,
/// so his left is heading + π/2).
[[nodiscard]] float grabMoveHeading(GrabMove move, float heading);

/// The struggle's delay (`GrabbedGoal_Process`, kind 31): the chain delay × (1 − max(0, (health − h) / (1 − h))), for
/// `health` the share of health left and `h` the class's hurt fraction; a healthy man struggles at once and a hurt one
/// waits the whole chain delay.
[[nodiscard]] int struggleDelayMs(int chainDelayMs, float health, float hurtFraction);

/// The get-up attack's delay (`GroundedGoal_Process`): max(0, 1900 − min(time down, 2000)) ms.
[[nodiscard]] int getUpDelayMs(std::uint64_t downMs);

/// The grabbing goal (`0x12`, the grabber).
/// @orig 0x002b5a98 GrabbingGoal_Init (unknown)
class GrabbingGoal final : public Goal {
  public:
    GrabbingGoal() : Goal(GoalType::ReactGrabbing) {}

    /// Rolls the hand-over flag: `Random_Int(100)` < the gang's `CfgGang` value 5 × 10; cleared under a HoldFlag goal
    /// or a FollowAndDefend one (**Coney stand-in**: TrackHumanGoal, which Coney gives for FollowAndDefend).
    /// @orig 0x002b5ad0 GrabbingGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Done when no longer grabbing. The held man becomes the target; while actions are queued it waits. A front grab
    /// with the flag, the man under two or more attackers, spins him into a rear hold (command `0x19`, after 33 ms). A
    /// rear grab with the flag, while the man has other than one attacker, or while presenting him to a friendly player
    /// (a rear grab, the nearest player friendly, not busy and within 3 m, for up to 3 s), turns to face the nearest
    /// player among them (else the nearest active attacker) and waits: holding him up for a friend to hit. Otherwise it
    /// picks a kind: kind 24 (a strike in the grab) twice 40 % of the time, the throws 25 and 29 with the stick toward
    /// grabMoveDirection(), the power strikes 26-28 without one. **Coney stand-ins**: no trains; the presenting does
    /// not test that player 1's Warrior command is not Defend (the command lives in the story, out of the brain's
    /// reach).
    /// @orig 0x002b60c0 GrabbingGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Clears the actions.
    /// @orig 0x002b5b58 GrabbingGoal_End (unknown)
    void end(Brain& brain) override;

    /// The hand-over flag (`+0x14`).
    [[nodiscard]] bool handsOver() const { return m_handOver; }

  private:
    // Whether he presents the man to a friendly player (step 4); the 3 s run from the first update that could.
    bool presenting(Brain& brain);

    bool m_handOver = false;                       // +0x14
    std::optional<std::uint64_t> m_presentUntilMs; // +0x10
};

/// The throw's or push's direction for `brain` grabbing `held` (`Grabbing_PickMove`), the first rule that gives one:
/// away from his HoldFlag goal's flag; for a power class that throws at walls, a random wall next to them (the man's
/// sectors 4, 6 and 2 and the grabber's 4), else a random side where a human not friendly to the grabber stands; away
/// from the human his gang's Defend tactic defends; else a random left, ahead or right (never behind). The
/// not-friendly sides are the same four sectors, each with flag 1 and a nearest man not a friend (the test behind the
/// grabber reads his flag with the man's sector-4 human, as the original does).
/// @orig 0x002b5b98 Grabbing_PickMove (unknown)
[[nodiscard]] GrabMove grabMoveDirection(Brain& brain, Brain& held);

/// The mounting goal (`0x13`, the tackler on top): done when no longer tackling; the held man the target; a kind
/// picked, 45 and 36 doing nothing, kind 35 (the ground punches) twice 40 % of the time.
/// @orig 0x002b65f8 MountingGoal_Init (unknown)
class MountingGoal final : public Goal {
  public:
    MountingGoal() : Goal(GoalType::ReactTackling) {}
    /// One update, as above. **Coney stand-in**: no trains.
    /// @orig 0x002b6638 MountingGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Clears the actions.
    void end(Brain& brain) override;
};

/// The grabbed goal (`0x14`) and the mounted goal (`0x15`): the held man's struggle.
/// @orig 0x002b6818 GrabbedGoal_Init (unknown)
/// @orig 0x002b6b88 MountedGoal_Init (unknown)
class HeldGoal final : public Goal {
  public:
    /// The grabbed goal (`mounted` false) or the mounted goal.
    explicit HeldGoal(bool mounted) : Goal(mounted ? GoalType::ReactTackled : GoalType::ReactGrabbed) {}
    /// Done once free. A friend's hold is left alone; with threat response 0 (but for a dealer or a
    /// class-221 human, the dogs) he only waits;
    /// otherwise the holder becomes the target and, once the actions are done, he picks a kind: the struggle strike
    /// 31 after struggleDelayMs() (a class-221 human with a hurt fraction of 0); grabbed, any other kind with the stick
    /// along his heading or behind him (50/50); mounted, every kind after the struggle's delay with no stick. Once
    /// mugged (grabbed only), he no longer struggles. **Coney stand-ins**: the civilian's help call is not built; the
    /// queue must be empty before a pick (the original's wait is not traced); a held AI's presses reach no handler yet
    /// (Coney's held AI is driven by its holder).
    /// @orig 0x002b6848 GrabbedGoal_Process (unknown)
    /// @orig 0x002b6bb8 MountedGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Clears the actions.
    void end(Brain& brain) override;

  private:
    bool m_mugged = false; // +0x10
};

/// The grounded goal (`0x17`, knocked down).
/// @orig 0x002b49f8 GroundedGoal_Init (unknown)
class GroundedGoal final : public Goal {
  public:
    GroundedGoal() : Goal(GoalType::ReactKnockedDown) {}
    /// Notes when he went down.
    /// @orig 0x002b4a38 GroundedGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Done once up. While someone holds an attack slot on him, queues kind 42 (the get-up attack) on himself after
    /// getUpDelayMs(), once. **Coney stand-in**: no shadows, so no target is dropped for one.
    /// @orig 0x002b4aa8 GroundedGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Clears the actions.
    void end(Brain& brain) override;

  private:
    std::uint64_t m_downAtMs = 0;
    bool m_queued = false;
};

} // namespace coney::ai
