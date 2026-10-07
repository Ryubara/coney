// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "ai/tactic.h"

// TacticCrowd: turns a gang into onlookers round a fight. Each member is given one goal, on top of his own, that keeps
// it in place: an idle goal when the crowd cheers, a spectate goal of 4-6 s when it only watches. A cheering crowd
// takes no hit reactions and never fights, and every 2 s sends its next member into a cheer; a watching crowd has a
// free member gesture now and then. TacticTrigger switches the reactions: on demand, every free member cheers (two
// clips). Research: docs/research/ai.md#tactics

namespace coney::ai {

/// The crowd tactic's type id.
inline constexpr int kCrowdTactic = 0x1b;
/// Its anims: the watchers' gesture (`0x25c`), the reactions' first clips (`0x8f`, `0x10` at 50 % when only the
/// periodic switch asked, `0xb1` when a trigger switched them off) and their cheer (`0x256`).
inline constexpr int kCrowdWatchAnim = 0x25c;
inline constexpr int kCrowdReactAnim = 0x8f;
inline constexpr int kCrowdReactAltAnim = 0x10;
inline constexpr int kCrowdReactOffAnim = 0xb1;
inline constexpr int kCrowdCheerAnim = 0x256;

/// The crowd tactic (vtable `0x005437a0`).
/// @orig 0x0030f4d0 TacticCrowd_Init (unknown)
class TacticCrowd final : public Tactic {
  public:
    /// A crowd that cheers (`TacticCrowd`'s option) or only watches, calling `callback` (empty for none); its first
    /// tick is 2 s after `nowMs`.
    TacticCrowd(std::string callback, bool cheering, std::uint64_t nowMs);

    /// Seats the members (each living one given its goal above his goal base); a cheering crowd's members take no hit
    /// reactions and have threat response 0, and its first cheer is 2 s on. **Coney choices**: the anim substitutions
    /// (cheers for `0x256`, 599 and 668; `0x25c` for watchers), the cheer idles and `0x001695b8` are not built.
    /// @orig 0x0030f6b0 TacticCrowd_Start (unknown)
    void start(Gang& gang) override;
    /// Every 1-2 s: a watching crowd has its first free member (record clear) gesture (`0x25c`) at 51 %; a cheering one
    /// runs its reactions (when switched on). Every 2 s a cheering crowd turns to its next member to cheer
    /// (cheerTurn()). Always 0.
    /// @orig 0x0030fe78 TacticCrowd_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// `0x10` (a member warned of an attack) fires the callback with 6; `0x14` (violence nearby, its strength the
    /// event's value) makes a free member of a cheering crowd react at min(strength, 100) % when the strength is above
    /// 29, and is used; `0x13` and `0x16` seat the crowd again.
    /// @orig 0x00310100 TacticCrowd_Event (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;
    /// `TacticTrigger(gang, what, on)`: what 0 switches the periodic reactions; what 1 has every free member react at
    /// once. Other values do nothing.
    /// @orig 0x00316fa0 Tactic_TriggerCrowd (unknown)
    void trigger(Gang& gang, int what, bool on);

    /// Whether it cheers (`+0x28`), whether its periodic reactions are on (`+0x2a`), and the member whose turn it is
    /// to cheer (`+0x2b`, an index among the living).
    [[nodiscard]] bool cheering() const { return m_cheering; }
    [[nodiscard]] bool reactionsOn() const { return m_reactions; }
    [[nodiscard]] int cheerTurn() const { return m_cheerTurn; }

  private:
    // Each living member given its goal above his goal base: idle when cheering, else spectate for 4-6 s.
    // @orig 0x0030f570 TacticCrowd_Seat (unknown)
    void seat(Gang& gang);
    // Each free member with no actions queues its reaction (a clip, then the cheer), when the switch is on or `all`;
    // `on` is the trigger's.
    // @orig 0x0030fc48 TacticCrowd_React (unknown)
    void react(Gang& gang, bool all, bool on);
    // Queues `member`'s reaction: `first`, then the cheer.
    void queueReaction(Gang& gang, Brain& member, int first, int minDelayMs, int maxDelayMs);

    bool m_cheering;                 // +0x28
    bool m_reactions = false;        // +0x2a
    int m_cheerTurn = 1;             // +0x2b
    std::uint64_t m_nextTickMs;      // +0x20
    std::uint64_t m_nextCheerMs = 0; // +0x24
};

} // namespace coney::ai
