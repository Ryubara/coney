// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/goal.h"

// The block goal (type `0x1b`) and the try that pushes it. When an attack on its human was announced, a brain may push
// the block goal for 1-3 s. For an AI it never makes a block: the dispatcher starts one only on R1 held on a real pad,
// and an AI has none, so its command 4 (R1 held) does nothing. What it does give an AI: hit reactions off for its first
// updates (the hits still take health), a counter (command 3) rolled on every update of the block time, which answers
// a grab or a tackle, and a punishing attack in the block's last second once the block was extended.
// Research: docs/research/ai.md#block, docs/research/combat.md#block

namespace coney::ai {

class Brain;

/// How long a block lasts, and each extension of it: a random 1-3 s.
inline constexpr int kBlockMinMs = 1000;
inline constexpr int kBlockMaxMs = 3000;
/// The goal's update from which it turns the hit reactions back on (once the human is free).
inline constexpr int kBlockReactionsBackUpdate = 6;
/// The block's last second, in which an extended block queues its punishing attack.
inline constexpr int kBlockLastMs = 1000;
/// The record `+0x08` bits that keep the hit reactions off past kBlockReactionsBackUpdate (`0x5c7eae0`).
inline constexpr std::uint32_t kBlockBusyFlags = 0x05c7eae0;
/// The attack kinds a punishing attack picks from (`0x224778ffff0000`, bit k for kind k).
inline constexpr std::uint64_t kPunishingKinds = 0x00224778ffff0000ULL;

/// The block goal.
/// @orig 0x002b54d8 BlockGoal_Init (unknown)
class BlockGoal final : public Goal {
  public:
    BlockGoal() : Goal(GoalType::Block) {}

    /// Sets the block's end (`+0x10`, now + 1-3 s), rolls whether a duck may counter (`+0x14`: rand100 under the block
    /// chance; read only by the duck's counter window, which an AI never reaches) and keeps whether the hit reactions
    /// were already off (`+0x18`); with a target, turns them off (human `+0xe0` `0x800`). **Coney choice**: the
    /// pattern read that sets `+0x15` here is not built (the Warriors' threshold is 0, so it never reads).
    /// @orig 0x002b5520 BlockGoal_Start (unknown)
    void start(Brain& brain) override;
    /// One update, in the original's order: the hit reactions back on from the sixth update once the human is free; a
    /// busy or downed target ends the block; actions queued wait; an ended block is done; a block time run out is
    /// extended (and `+0x15` set) while the target still attacks the human, else ended; in the block time's last
    /// second an extended block queues a punishing attack; otherwise command 3 when counterRoll() says so, else
    /// command 4.
    /// @orig 0x002b5808 BlockGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Turns the hit reactions back on, unless they were off before the goal started.
    void end(Brain& brain) override;

    /// The counter, on one update of the block time with a target and `+0x15` clear: rand100 under the counter chance
    /// (`Human_CounterChance`) and counterTest(). The roll is drawn whether or not the test passes.
    [[nodiscard]] bool counterRoll(Brain& brain) const;

    /// When the block time runs out, ms (`+0x10`).
    [[nodiscard]] std::uint64_t untilMs() const { return m_untilMs; }
    /// Whether a duck may counter (`+0x14`).
    [[nodiscard]] bool mayCounter() const { return m_mayCounter; }
    /// Whether the block was extended (or the pattern read): no counter roll, a punishing attack instead (`+0x15`).
    [[nodiscard]] bool patterned() const { return m_patterned; }
    /// Whether the block is still on (`+0x16`).
    [[nodiscard]] bool active() const { return m_active; }

  private:
    // Turns the hit reactions off, unless they were off before the goal started.
    void reactionsOff(Brain& brain) const;

    std::uint64_t m_untilMs = 0;     // +0x10
    bool m_mayCounter = false;       // +0x14
    bool m_patterned = false;        // +0x15
    bool m_active = true;            // +0x16
    int m_updates = 0;               // +0x17
    bool m_reactionsWereOff = false; // +0x18
    bool m_reactionsBack = false;    // the hit reactions were turned back on
};

/// The counter test (`0x0027d6e0`'s): the human free (record `+0x08` none of `0xfc7eaf7`) and not hurt, its counter
/// chance above 0, a brain of type 3 (the gate `0x00223e20` while the byte `*(0x0051489c) + 0x56e3` is 0), and its
/// target aiming at it, on its feet, face to face (combat::faceToFace(), inferred) and playing a grab's intro (69-71)
/// or a tackle's (2-4) (combat::aiCounterFor()). **Coney reading**: the gate's other way in, a class whose `+0x11b`
/// is 13, is left out (Coney does not keep that class byte), and the byte is taken as 0.
[[nodiscard]] bool counterTest(const Brain& brain);

/// The fight goal's block try: with an attack announced (`+0x200` not 0), a roll under the block chance (a quarter of
/// it for a type-3 brain) pushes the block goal. Returns whether it did. **Coney choice**: the pattern-reading rule
/// (a player's attacks always blocked at the class's `+0x37`) is not built.
/// @orig 0x0029f098 Goal_TryBlock (unknown)
bool tryBlock(Brain& brain);

} // namespace coney::ai
