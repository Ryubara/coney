// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "animation/anim_math.h"
#include "characters/anim_set.h"
#include "combat/meters.h"
#include "combat/power_class.h"
#include "combat/reactions.h"
#include "combat/stick.h"
#include "human/human_animator.h"

// The part of a human that takes hits, shared by the player and the sandbox's target: the update's largest hit, the
// reaction it plays on its feet (a plain one, a stun or a knockdown), the stun's and the ground's timers, getting up,
// and the knocked-down human's mashing. Its owner decides when a hit reaches it (a block, the hit armour, a hold) and
// what else it is doing; this plays the reaction clips through the owner's animator.
// Research: docs/research/combat-reactions.md#reactions, docs/research/combat-reactions.md#being-hit-runtime

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
    /// The strike's material for its hit sound (human::strikeMaterial(), `Hit_ResolveBlock`); 0 for none.
    std::uint32_t strikeMaterial = 0;
    /// The attacker holds `0x1400000` (the run attack, the charge or the dive): the bodies' `HUMAN` pair sounds too.
    bool charge = false;
    /// Who struck, as the run log names him (Fighter::logName()); empty when no human did. Coney's own, for the log.
    std::string attackerName{};
};

/// What a human taking hits is, for its reaction (docs/research/combat/index.md#hit-codes).
struct VictimFrame {
    anim::Vec3 position;      ///< Its feet.
    float heading = 0.0F;     ///< Radians, 0 facing +y.
    bool hurt = false;        ///< Below its class's hurt fraction.
    bool flag400 = false;     ///< Human flag `0x400` (the player has it).
    bool flag200 = false;     ///< Human flag `0x200` (`HuSetReducedReact`): strength - 1.
    bool flag80 = false;      ///< Human flag `0x80` (`HuSetUngroundable`): strength at most 1.
    bool unstunnable = false; ///< Human flag `0x100` (`HuSetUnstunnable`): never stunned.
};

/// The bits of a human's state word that the death chain reads (`Human_PlayDeathReaction`'s tests, each a
/// `Human_HasStateFlag` call: docs/research/combat-reactions.md#knockout-timing).
namespace human_state {
inline constexpr std::uint64_t kGrabbedFront = 0x10;   ///< `Human_IsGrabbedFromFront`.
inline constexpr std::uint64_t kGrabbedRear = 0x20;    ///< `Human_IsGrabbedFromRear`.
inline constexpr std::uint64_t kGrabbingFront = 0x40;  ///< `Human_IsGrabbingFront`.
inline constexpr std::uint64_t kGrabbingRear = 0x80;   ///< `Human_IsGrabbingRear`.
inline constexpr std::uint64_t kTackling = 0x400;      ///< `Human_IsTackling`.
inline constexpr std::uint64_t kTackled = 0x800;       ///< `Human_IsTackled`.
inline constexpr std::uint64_t kOnFire = 0x4000;       ///< `Human_IsOnFire`.
inline constexpr std::uint64_t kWounded = 0x10000;     ///< `Human_IsWounded`.
inline constexpr std::uint64_t kCuffed = 0x20000;      ///< `Human_IsCuffed`.
inline constexpr std::uint64_t kKnockedOut = 0x40000;  ///< `Human_IsKnockedOut`.
inline constexpr std::uint64_t kKnockedDown = 0x80000; ///< `Human_IsKnockedDown`.
/// Cuffed, knocked out or knocked down (`Human_HasStateE0000`).
inline constexpr std::uint64_t kDown = kCuffed | kKnockedOut | kKnockedDown;
inline constexpr std::uint64_t kState8000000 = 0x8000000; ///< `Human_HasState8000000`.
} // namespace human_state

/// What a human is as it dies: the state bits (human_state) and the flag the death chain picks its clips by.
struct DyingState {
    std::uint64_t state = 0; ///< Its state word's bits, of human_state.
    bool woundable = false;  ///< Human flag `0x8` (flag::kWoundable).
};

/// A dying clip the caller names, from another human's set: a paired move's victim side that defeats it
/// (`Human_PlayDeathReaction`'s clip and the attacker given as the set's owner, docs/research/stealth.md#stealth-kill).
struct NamedDying {
    std::uint32_t clip = 0;
    const characters::AnimSet* from = nullptr; ///< The attacker's set, which the clip and its rate come from.
};

/// One clip of the death chain before its loop, and the blend it hands over to the next clip with.
struct DeathStep {
    std::uint32_t clip = 0;
    float blend = 0.0F; ///< Seconds.
};

/// The death chain Victim::die() played: its clips in play order (the dying reaction, the fall, 205), the last of which
/// ends in the knock-out, then the loop.
struct DeathChain {
    std::vector<DeathStep> steps;
    std::uint32_t loop = 0;
};

/// What a reaction on its feet turned out to be.
enum class ReactionKind : std::uint8_t {
    Plain,     ///< The reaction, then the idle.
    Stun,      ///< The reaction, then 355 and the stun's loop until its time passes, then 357 and the idle.
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
    /// class's stun time; never for a victim with flag `0x100`), else a plain reaction that returns to `idle`. A hit
    /// during the rise 199 cuts it without its end (`Human_GetUp` does not run): the victim is down again under the
    /// standing reaction and rises with 199 once it ends (step()). Returns the kind; `lastReaction()` names the clip.
    /// Research: docs/research/combat-reactions.md#stun-end
    /// @orig 0x0026a6d0 Human_PlayReaction (unknown)
    /// @orig 0x0022f658 Human_Stun (unknown)
    /// @orig 0x0022f100 Human_KnockDown (unknown)
    ReactionKind react(const IncomingHit& hit, const VictimFrame& frame, HumanAnimator& animator, std::uint32_t idle,
                       std::uint64_t nowMs);
    /// Plays the death chain for `hit` on a human that is `dying` (docs/research/combat-reactions.md#knockout-timing)
    /// and lies on the ground for good: the dying clip of its special state (else the hit's own: the `DIE` set half the
    /// time, else 292), then a fall (308 when the clip has no knockdown event and the human is not down, 198 when its
    /// event's value is 0), then 205 for a woundable human not yet wounded, then the loop (200 after 205, 323 cuffed,
    /// else 349), each clip holding `0x2000`. Its owner knocks it out as the last clip before the loop ends. The hit
    /// waiting is dropped, and `health`, run out, reads 1 (docs/research/combat-reactions.md#down-health). deathChain()
    /// keeps the clips and their blends. With `named` the dying clip is that one, from its set (no special state's or
    /// hit's clip is picked), and its knockdown event decides the fall.
    /// Research: docs/research/combat-reactions.md#knockout-timing
    /// @orig 0x00267a00 Human_PlayDeathReaction (unknown)
    void die(const IncomingHit& hit, const VictimFrame& frame, const DyingState& dying, combat::Health& health,
             HumanAnimator& animator, const NamedDying* named = nullptr);
    /// The chain the last die() played.
    [[nodiscard]] const DeathChain& deathChain() const { return m_deathChain; }

    /// The ground starts (a knockdown, a throw's landing) at `nowMs`; with `stunned` the stun lasts until the rise +
    /// the stun time (`Human_SetDownTimers`' end, which the rise's end replaces: step()). The doublings by the attack's
    /// clip flags `0x100` / `0x200`, a class-13 attacker's 2 s and flag `0x80`'s ground of no time are not ported
    /// (#685). Research: docs/research/characters/index.md#code-core
    /// @orig 0x0022f4f8 Human_SetDownTimers (unknown)
    void knockDown(std::uint64_t nowMs, bool stunned);
    /// Stuns it from `nowMs` for the class's stun time. The doubling by the attack's clip flag `0x200` is not ported
    /// (#685); flag `0x100`'s refusal is react()'s.
    /// Research: docs/research/characters/index.md#code-core
    /// @orig 0x0022f9e0 Human_SetStunTimer (unknown)
    void stun(std::uint64_t nowMs);
    /// Forgets the stun and the ground (a hold takes it, it got up another way).
    /// @coney-only Coney's reset of its own timers; the original clears each state where it ends
    void clear();
    /// Stunned until `untilMs`, the class's stun time aside (a boss's tired window), already in the stun's loop.
    /// @coney-only Coney's boss tired window sets its own time (Fighter::stunFor carries `Human_Stun`)
    void stunUntil(std::uint64_t untilMs) {
        m_stunUntilMs = untilMs;
        m_stunEntered = true;
        m_stunTimeOver = false;
    }
    /// Forgets the stun only.
    /// @coney-only Coney's reset of its own stun timer (a script's or a scene's clear)
    void endStun() {
        m_stunUntilMs = 0;
        m_stunTimeOver = false;
    }
    /// The stun's time is over now, whatever it was set to: standing, this update's step() ends the stun once no clip
    /// drives the human, as a time already past would; lying down, the stun is forgotten at once (`Human_EndStun`
    /// from outside the down update clears it when knocked down).
    /// @coney-only Coney's victim ends a standing stun in step(), so a stun ended from outside marks its time over
    void expireStun() {
        if (m_grounded) {
            endStun();
        } else {
            m_stunTimeOver = m_stunUntilMs != 0;
        }
    }

    /// One update's timers at `nowMs`. Standing stunned with no clip driving the human (a reaction, the rise or 355 is
    /// not cut short, the stun's loop 356 is): once its time has passed the stun ends, its state clearing as 357
    /// starts (then `idle`); before, the clip that ended returns to 355, then 356. A grounded victim with `canRise`
    /// and no clip driving it gets up with 199 once its ground time has passed (then the stun's loop 356 while
    /// stunned, else `idle`); as 199 ends, a stun's time starts again from that update. Returns true on the update it
    /// rises.
    /// Research: docs/research/combat-reactions.md#stun-end
    /// @orig 0x00256a60 Human_UpdateDown (unknown)
    /// @orig 0x0022f8d8 Human_EndStun (unknown)
    /// @orig 0x0022f3c8 Human_GetUp (unknown)
    bool step(HumanAnimator& animator, std::uint32_t idle, std::uint64_t nowMs, bool canRise);
    /// A command made while lying down: once the knockdown's reaction is over (lying in 196) it cuts the ground time
    /// by combat::mashCut(), up to the update that lands on the rise's time; during the reaction it does nothing. A
    /// stun after the rise is not cut (its time starts again as 199 ends). Returns whether it cut.
    /// @orig 0x00256a60 Human_UpdateDown (unknown)
    bool mash(const HumanAnimator& animator, std::uint64_t nowMs);

    [[nodiscard]] const combat::PowerClass& powerClass() const { return m_class; }
    /// On the ground (from a knockdown or a throw) until it rises.
    [[nodiscard]] bool grounded() const { return m_grounded; }
    /// Stunned (state `0x100000`): from the stun until it ends, which is after its time has passed, once no clip
    /// drives the human (step()).
    [[nodiscard]] bool stunned() const { return m_stunUntilMs != 0; }
    /// When the stun's time passes, ms of game time (0 when not stunned).
    [[nodiscard]] std::uint64_t stunUntilMs() const { return m_stunUntilMs; }
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
    std::uint64_t m_stunUntilMs = 0; // 0 when not stunned; kept past its time until the stun ends
    bool m_stunEntered = false;      // 355 has played since the clip the stun began in
    bool m_stunTimeOver = false;     // the stun's time was ended from outside (expireStun())
    bool m_rising = false;           // 199 plays: its end restarts a stun's time (Human_GetUp)
    std::uint64_t m_riseAtMs = 0;
    int m_reactions = 0;
    int m_stuns = 0;
    int m_knockdowns = 0;
    int m_lastReaction = -1;
    DeathChain m_deathChain;
};

} // namespace coney::human
