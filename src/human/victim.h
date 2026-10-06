// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation/anim_math.h"
#include "combat/power_class.h"
#include "combat/reactions.h"
#include "combat/stick.h"
#include "human/human_animator.h"

// The part of a human that takes hits, shared by the player and the sandbox's target: the update's largest hit, the
// reaction it plays on its feet (a plain one, a stun or a knockdown), the stun's and the ground's timers, getting up,
// and the knocked-down human's mashing. Its owner decides when a hit reaches it (a block, the hit armour, a hold) and
// what else it is doing; this plays the reaction clips through the owner's animator.
// Research: docs/research/combat.md#reactions, docs/research/combat.md#being-hit-runtime

namespace coney::human {

/// One hit as it reaches a human: what the attacker's strike carries. A future AI attacker and the tests build it the
/// same way the player's fighter does.
struct IncomingHit {
    int damage = 0;                  ///< The attacker's Anim Range List damage for the attack (taken as it is).
    int attackAnim = -1;             ///< The attacker's anim id.
    int code = 0;                    ///< The attack's hit code (Anim Range List `+0x0c`).
    std::uint16_t flags = 0;         ///< The attack's flags (`+0x0e`): combat::kRangeFlagStun stuns.
    anim::Vec3 attacker;             ///< Where the attacker's feet are.
    bool react = true;               ///< False for a move inside a hold, whose victim clips the attacker plays.
    bool ignoresArmour = false;      ///< The attacker is a player, raging, or has flag `0x200000` or `0x4000`.
    bool attackerFlag200000 = false; ///< The attacker's flag `0x200000` (strength + 1).
    bool attackerIsPlayer = false;   ///< A player's hit: its reaction shakes the attacker's camera.
};

/// What a human taking hits is, for its reaction (docs/research/combat.md#hit-codes).
struct VictimFrame {
    anim::Vec3 position;  ///< Its feet.
    float heading = 0.0F; ///< Radians, 0 facing +y.
    bool hurt = false;    ///< Below its class's hurt fraction.
    bool flag400 = false; ///< Human flag `0x400` (the player has it).
};

/// What a reaction on its feet turned out to be.
enum class ReactionKind : std::uint8_t {
    Plain,     ///< The reaction, then the idle.
    Stun,      ///< The reaction, then the stun's loop until its time passes, then 357 and the idle.
    Knockdown, ///< The reaction, then the ground (196) until it gets up with 199.
};

/// The update's hit, the reaction and the timers of a human that takes hits.
class Victim {
  public:
    /// A victim of power class `powerClass`, its coin flips (the dying clip, the mash's cut) seeded with `seed`.
    explicit Victim(const combat::PowerClass& powerClass, std::uint32_t seed = 1);

    /// Keeps `hit` when it is the update's largest (combat::PendingDamage's rule).
    void hit(const IncomingHit& hit);
    /// Whether a hit waits this update.
    [[nodiscard]] bool pending() const { return m_hasPending; }
    /// Takes the waiting hit (the caller applies it).
    IncomingHit takePending();

    /// The reaction's input for `hit` on a victim standing as `frame`.
    [[nodiscard]] static combat::ReactionInput reactionInput(const IncomingHit& hit, const VictimFrame& frame);

    /// Plays the reaction to `hit` on its feet through `animator` at game time `nowMs`: a knockdown when the reaction's
    /// clip has an event of type 7 (the ground for the class's ground time; stunned too when the attack stuns or it
    /// already is, until the rise + the stun time), else a stun when the attack's flags stun or it already is (the
    /// class's stun time), else a plain reaction that returns to `idle`. Returns the kind; `lastReaction()` names the
    /// clip.
    /// @orig 0x0026a6d0 Human_PlayReaction (unknown)
    /// @orig 0x0022f658 Human_Stun (unknown)
    /// @orig 0x0022f100 Human_KnockDown (unknown)
    ReactionKind react(const IncomingHit& hit, const VictimFrame& frame, HumanAnimator& animator, std::uint32_t idle,
                       std::uint64_t nowMs);
    /// Plays the dying reaction for `hit` (the `DIE` set half the time, else 292) and the ground for good.
    void die(const IncomingHit& hit, const VictimFrame& frame, HumanAnimator& animator);

    /// The ground starts (a knockdown, a throw's landing) at `nowMs`; with `stunned` the stun lasts until the rise +
    /// the stun time.
    void knockDown(std::uint64_t nowMs, bool stunned);
    /// Stuns it from `nowMs` for the class's stun time.
    void stun(std::uint64_t nowMs);
    /// Forgets the stun and the ground (a hold takes it, it got up another way).
    void clear();

    /// One update's timers at `nowMs`: a stun whose time has passed ends with 357 (then `idle`) once the clip playing
    /// is over (a reaction or the rise is not cut short); a grounded victim with `canRise` gets up with 199 once its
    /// ground time has passed (then the stun's loop while stunned, else `idle`). Returns true on the update it rises.
    /// @orig 0x00256a60 Human_RefillMeters (unknown)
    /// @orig 0x0022f8d8 Human_EndStun (unknown)
    bool step(HumanAnimator& animator, std::uint32_t idle, std::uint64_t nowMs, bool canRise);
    /// A command made while lying down: once the knockdown's reaction is over (lying in 196) it cuts the ground time
    /// by combat::mashCut(); during the reaction it does nothing. The stun after the rise moves with it (**Coney's
    /// choice**). Returns whether it cut.
    bool mash(const HumanAnimator& animator, std::uint64_t nowMs);

    [[nodiscard]] const combat::PowerClass& powerClass() const { return m_class; }
    /// On the ground (from a knockdown or a throw) until it rises.
    [[nodiscard]] bool grounded() const { return m_grounded; }
    /// Stunned: its stun's time has not passed.
    [[nodiscard]] bool stunned() const { return m_stunUntilMs != 0; }
    /// The stun has passed but its 357 waits for the clip playing to end.
    [[nodiscard]] bool stunExitPending() const { return m_stunExitPending; }
    /// When a grounded victim gets up, ms of game time.
    [[nodiscard]] std::uint64_t riseAtMs() const { return m_riseAtMs; }
    /// Reactions played, stuns and knockdowns so far, and the last reaction's clip (-1 before any).
    [[nodiscard]] int reactions() const { return m_reactions; }
    [[nodiscard]] int stuns() const { return m_stuns; }
    [[nodiscard]] int knockdowns() const { return m_knockdowns; }
    [[nodiscard]] int lastReaction() const { return m_lastReaction; }

  private:
    // Whether anim `id`'s clip in `animator`'s set has a knockdown event (type 7).
    [[nodiscard]] static bool knocksDown(const HumanAnimator& animator, int id);

    combat::PowerClass m_class;
    combat::CombatRandom m_random;
    IncomingHit m_pending;
    bool m_hasPending = false;
    bool m_grounded = false;
    bool m_stunExitPending = false;
    std::uint64_t m_stunUntilMs = 0; // 0 when not stunned
    std::uint64_t m_riseAtMs = 0;
    int m_reactions = 0;
    int m_stuns = 0;
    int m_knockdowns = 0;
    int m_lastReaction = -1;
};

} // namespace coney::human
