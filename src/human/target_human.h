// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/anim_set.h"
#include "combat/meters.h"
#include "combat/stick.h"
#include "human/human_animator.h"

// A passive human to fight: Coney's own test target for the sandbox, not part of the original game and never placed in
// a real level. It stands where its layout line puts it, takes the player's hits as pending damage, loses health and
// reacts as the research's victim does (the reaction table, the stun, the knockdown and getting up, with a street
// civilian's power class); it has no brain, does not walk and never fights back.
// Research: docs/research/combat.md#hit-codes, docs/research/combat.md#reactions, docs/research/combat.md#grab
// Guide: docs/guides/sandbox.md

namespace coney::human {

/// How the target lies: what the player's moves can do to it.
enum class TargetState : std::uint8_t {
    Standing, ///< On its feet: strikes, grabs and tackles reach it.
    Held,     ///< In the player's grab or under a tackle's intro.
    Mounted,  ///< Tackled to the ground with the player on it.
    Grounded, ///< On the ground after a throw or a knockdown, or knocked out at 0 health.
};

/// What drawing a target needs from one step.
struct TargetSnapshot {
    anim::Vec3 feet;
    float heading = 0.0F; ///< Radians, 0 facing +y.
    anim::Pose pose;
};

/// The snapshot `alpha` of the way from `previous` to `current` (as human::interpolate() blends the player's).
[[nodiscard]] TargetSnapshot interpolate(const TargetSnapshot& previous, const TargetSnapshot& current, float alpha);

/// One hit on a target.
struct TargetHit {
    int damage = 0;
    int attackAnim = -1;     ///< The attacker's anim id.
    int code = 0;            ///< The attack's hit code (Anim Range List `+0x0c`).
    std::uint16_t flags = 0; ///< The attack's flags (`+0x0e`): combat::kRangeFlagStun stuns.
    anim::Vec3 attacker;     ///< Where the attacker's feet are.
    bool react = true;       ///< False for a move inside a hold, whose victim clips the attacker plays.
};

/// The street civilian's power class (class 2, docs/research/characters.md#power-classes), a target's numbers.
struct TargetClass {
    float hurtFraction = 0.35F; ///< Below this share of its health it is hurt.
    int stunMs = 750;           ///< A stun's length.
    int groundMs = 2000;        ///< How long a knockdown keeps it down.
};

/// A passive target human.
class TargetHuman {
  public:
    /// A target playing `anims` (which must outlive it) through `slots`, posed over `bindRotations`, with `health` of
    /// `health` (at least 1), its feet at `position` facing `headingRadians`; its coin flips (which dying clip) seeded
    /// with `seed`.
    TargetHuman(const characters::AnimSet& anims, const AnimSlots& slots,
                std::span<const anim::Quat, anim::kPoseBones> bindRotations, int health, anim::Vec3 position,
                float headingRadians, std::uint32_t seed = 1);

    /// One update of 1/30 s: the update's largest hit is applied and reacted to (a reaction, a stun, a knockdown, or
    /// at 0 health a dying clip and the ground for good), the animation steps, a stun runs out with 357, and a target
    /// down long enough gets up with 199.
    /// @orig 0x00265f70 Human_ApplyPendingDamage (unknown)
    /// @orig 0x00256a60 Human_RefillMeters (unknown)
    void step();

    /// A hit this update; the update keeps its largest (combat::PendingDamage).
    void hit(const TargetHit& hit);
    /// Plays the victim's `clips`, then `loop`, in `state` (AnimState::Attack returns to the idle afterwards,
    /// AnimState::Hold keeps the loop), and takes `targetState`; a stun ends.
    void play(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state, TargetState targetState);
    /// Moves it to `position` facing `headingRadians` (a grab or a tackle puts it in front of the player).
    void place(anim::Vec3 position, float headingRadians);
    /// Turns it to face `point`.
    void face(anim::Vec3 point);

    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    [[nodiscard]] float heading() const { return m_heading; }
    [[nodiscard]] const combat::Health& health() const { return m_health; }
    [[nodiscard]] TargetState state() const { return m_state; }
    [[nodiscard]] const HumanAnimator& animator() const { return m_animator; }
    /// Stunned: a stun hit's reaction, until its time runs out.
    [[nodiscard]] bool stunned() const { return m_stunUntilMs != 0; }
    /// Hurt: below the class's hurt fraction of its health.
    [[nodiscard]] bool hurt() const { return m_health.fraction() < m_class.hurtFraction; }
    /// Damage taken so far.
    [[nodiscard]] int damageTaken() const { return m_health.maximum() - m_health.value(); }
    /// Hits that took health off, reactions played, stuns and knockdowns so far.
    [[nodiscard]] int hitsTaken() const { return m_hits; }
    [[nodiscard]] int reactions() const { return m_reactions; }
    [[nodiscard]] int stuns() const { return m_stuns; }
    [[nodiscard]] int knockdowns() const { return m_knockdowns; }
    /// The last reaction clip it played (-1 before any).
    [[nodiscard]] int lastReaction() const { return m_lastReaction; }
    [[nodiscard]] const TargetSnapshot& current() const { return m_current; }
    [[nodiscard]] const TargetSnapshot& previous() const { return m_previous; }

  private:
    // Game time in whole milliseconds.
    [[nodiscard]] std::uint64_t nowMs() const;
    // Reacts to the update's hit: a dying clip, a reaction with its stun or knockdown.
    void react(const TargetHit& hit);
    // Whether anim `id`'s clip has a knockdown event (type 7).
    [[nodiscard]] bool knocksDown(int id) const;
    // The snapshot of the state now.
    [[nodiscard]] TargetSnapshot capture() const;

    HumanAnimator m_animator;
    std::array<anim::Quat, anim::kPoseBones> m_bindRotations{};
    anim::Vec3 m_position;
    float m_heading = 0.0F;
    combat::Health m_health;
    combat::CombatRandom m_random;
    TargetClass m_class;
    std::uint32_t m_idle; // the idle clip of its slots
    TargetHit m_pending;
    bool m_hasPending = false;
    TargetState m_state = TargetState::Standing;
    std::uint64_t m_updates = 0;
    std::uint64_t m_stunUntilMs = 0; // 0 when not stunned
    std::uint64_t m_riseAtMs = 0;    // when a grounded target gets up
    int m_hits = 0;
    int m_reactions = 0;
    int m_stuns = 0;
    int m_knockdowns = 0;
    int m_lastReaction = -1;
    TargetSnapshot m_previous;
    TargetSnapshot m_current;
};

} // namespace coney::human
