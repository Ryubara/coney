// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/anim_set.h"
#include "combat/meters.h"
#include "combat/power_class.h"
#include "combat/stick.h"
#include "human/combatant.h"
#include "human/holdable.h"
#include "human/human_animator.h"
#include "human/victim.h"

// A passive human to fight: Coney's own test target for the sandbox, not part of the original game and never placed in
// a real level. It stands where its layout line puts it, takes the player's hits as pending damage, loses health and
// reacts as the research's victim does (the reaction table, the stun, the knockdown and getting up, with a street
// civilian's power class); it has no brain, does not walk and never fights back.
// Research: docs/research/combat.md#hit-codes, docs/research/combat.md#reactions, docs/research/combat.md#grab,
// docs/research/combat.md#grab-posing
// Guide: docs/guides/sandbox.md

namespace coney::human {

/// What drawing a target needs from one step.
struct TargetSnapshot {
    anim::Vec3 feet;
    float heading = 0.0F; ///< Radians, 0 facing +y.
    anim::Pose pose;
};

/// The snapshot `alpha` of the way from `previous` to `current` (as human::interpolate() blends the player's).
[[nodiscard]] TargetSnapshot interpolate(const TargetSnapshot& previous, const TargetSnapshot& current, float alpha);

/// One hit on a target (the same as any human's, human::IncomingHit).
using TargetHit = IncomingHit;

/// A passive target human.
class TargetHuman final : public Holdable {
  public:
    /// A target playing `anims` (which must outlive it) through `slots`, bones its clips leave out taking
    /// `defaultRotations` (the game's reference pose, anim::referenceRotations()), with `health` of `health` (at least
    /// 1), its feet at `position` facing `headingRadians`; its coin flips (which dying clip) seeded with `seed`.
    TargetHuman(const characters::AnimSet& anims, const AnimSlots& slots,
                std::span<const anim::Quat, anim::kPoseBones> defaultRotations, int health, anim::Vec3 position,
                float headingRadians, std::uint32_t seed = 1);

    /// One update of 1/30 s: the update's largest hit is applied and reacted to (a reaction, a stun, a knockdown, or
    /// at 0 health a dying clip and the ground for good), the animation steps, a stun runs out with 357 once its
    /// reaction is over, and a target down long enough gets up with 199. Not attached to a grabber, a clip that moves
    /// the body (a paired clip, a reaction, a throw's) moves and turns it by its root motion, as the original's does.
    /// @orig 0x00265f70 Human_ApplyPendingDamage (unknown)
    void step();

    /// A hit this update; the update keeps its largest (combat::PendingDamage).
    void hit(const TargetHit& hit) override;
    /// A warning does nothing: the target never blocks.
    void warn(const AttackNotice& /*notice*/) override {}
    /// The target itself: it can be grabbed and tackled.
    [[nodiscard]] Holdable* holdable() override { return this; }
    /// Plays the victim's `clips`, then `loop`, in `state` (AnimState::Attack returns to the idle afterwards,
    /// AnimState::Hold keeps the loop), and takes `targetState`; a stun ends.
    void play(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state,
              TargetState targetState) override;
    /// The victim's side of a paired move: plays `clips` from `attacker`'s anim set at its rates (the grabber's
    /// reaction clips, docs/research/formats/animation.md#paired-tasks), then its own `loop`, switching at once (no
    /// fade), as play() does otherwise.
    void playPaired(std::span<const std::uint32_t> clips, const characters::AnimSet& attacker, std::uint32_t loop,
                    AnimState state, TargetState targetState) override;
    /// Attaches it to its grabber (Coney's stand-in for `Human_MoveAttached`, `0x00244e78`): while attached the
    /// grabber places it each update and its own root motion does not move it; detached, a held target moves by its
    /// clip's root motion.
    void setAttached(bool attached) override { m_attached = attached; }
    [[nodiscard]] bool attached() const override { return m_attached; }
    /// Moves it to `position` facing `headingRadians` (a grab or a tackle puts it in front of the player).
    void place(anim::Vec3 position, float headingRadians) override;
    /// Turns it to face `point`.
    void face(anim::Vec3 point) override;

    [[nodiscard]] anim::Vec3 position() const override { return m_position; }
    [[nodiscard]] float heading() const override { return m_heading; }
    [[nodiscard]] const combat::Health& health() const override { return m_health; }
    [[nodiscard]] TargetState state() const override { return m_state; }
    [[nodiscard]] const HumanAnimator& animator() const { return m_animator; }
    [[nodiscard]] const characters::AnimSet& anims() const override { return m_animator.anims(); }
    /// Its power class: the street civilian's (combat::kCivilianPowerClass).
    [[nodiscard]] const combat::PowerClass& powerClass() const { return m_victim.powerClass(); }
    /// Stunned: a stun hit's reaction, until its time runs out.
    [[nodiscard]] bool stunned() const { return m_victim.stunned(); }
    /// Hurt: below the class's hurt fraction of its health.
    [[nodiscard]] bool hurt() const { return m_health.fraction() < m_victim.powerClass().hurtFraction; }
    /// Damage taken so far.
    [[nodiscard]] int damageTaken() const { return m_health.maximum() - m_health.value(); }
    /// Hits that took health off, reactions played, stuns and knockdowns so far.
    [[nodiscard]] int hitsTaken() const { return m_hits; }
    [[nodiscard]] int reactions() const { return m_victim.reactions(); }
    [[nodiscard]] int stuns() const { return m_victim.stuns(); }
    [[nodiscard]] int knockdowns() const { return m_victim.knockdowns(); }
    /// The last reaction clip it played (-1 before any).
    [[nodiscard]] int lastReaction() const { return m_victim.lastReaction(); }
    [[nodiscard]] const TargetSnapshot& current() const { return m_current; }
    [[nodiscard]] const TargetSnapshot& previous() const { return m_previous; }

  private:
    // Game time in whole milliseconds.
    [[nodiscard]] std::uint64_t nowMs() const;
    // Reacts to the update's hit: a dying clip, a reaction with its stun or knockdown.
    void react(const TargetHit& hit);
    // Takes `targetState`: the ground time starts, a hold's end detaches, a stun ends.
    void enter(TargetState targetState);
    // Its clip moves it by its root motion (velocity turned by the heading, and the turn).
    void applyRootMotion();
    // The snapshot of the state now.
    [[nodiscard]] TargetSnapshot capture() const;

    HumanAnimator m_animator;
    std::array<anim::Quat, anim::kPoseBones> m_defaultRotations{};
    anim::Vec3 m_position;
    float m_heading = 0.0F;
    combat::Health m_health;
    Victim m_victim;
    std::uint32_t m_idle; // the idle clip of its slots
    TargetState m_state = TargetState::Standing;
    bool m_attached = false; // placed by its grabber (setAttached())
    std::uint64_t m_updates = 0;
    int m_hits = 0;
    TargetSnapshot m_previous;
    TargetSnapshot m_current;
};

} // namespace coney::human
