// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "animation/anim_math.h"
#include "combat/being_hit.h"
#include "combat/meters.h"
#include "human/victim.h"

// What one human sees of another it fights: where it stands, whether it can be struck, its health, and the two
// messages an attack sends it (the clip's warning and the hit). The player fights the sandbox's passive targets
// (TargetHuman) and other humans (Human, an AI's or a second player's) through this one view, and so does an AI human,
// so a strike, its warning and its reaction work the same whoever gives or takes them.
// Research: docs/research/combat.md#block, docs/research/combat.md#damage, docs/research/ai.md#attack-action

namespace coney::human {

class TargetHuman;

/// How a human lies: what an attacker's moves can do to it.
enum class TargetState : std::uint8_t {
    Standing, ///< On its feet: strikes, grabs and tackles reach it.
    Held,     ///< In a grab or under a tackle's intro.
    Mounted,  ///< Tackled to the ground with the attacker on it.
    Grounded, ///< On the ground after a throw or a knockdown, or knocked out at 0 health.
};

/// What an attacker's clip tells its target before the hit (combat::warningBetween() finds it in the attacker's clip;
/// the attacker sends it when the target is within twice the attack's reach, docs/research/combat.md#block).
struct AttackNotice {
    combat::AttackWarning warning = combat::AttackWarning::Duck;
    int attackAnim = -1; ///< The attacker's anim id.
    int code = 0;        ///< The attack's hit code (for an early block reaction).
    anim::Vec3 attacker; ///< Where the attacker's feet are.
};

/// A human another human can fight.
class Combatant {
  public:
    Combatant() = default;
    Combatant(const Combatant&) = default;
    Combatant& operator=(const Combatant&) = default;
    Combatant(Combatant&&) = default;
    Combatant& operator=(Combatant&&) = default;
    virtual ~Combatant() = default;

    /// Its feet (game axes).
    [[nodiscard]] virtual anim::Vec3 position() const = 0;
    /// Radians, 0 facing +y.
    [[nodiscard]] virtual float heading() const = 0;
    /// Its velocity (m/s, game axes), which an attack steering onto it leads; still by default.
    [[nodiscard]] virtual anim::Vec3 velocity() const { return {}; }
    /// Its body scale (`+0x65c`): an attack steering onto a target scaled above 1.1 reaches 0.07 m farther.
    [[nodiscard]] virtual float bodyScale() const { return 1.0F; }
    /// How it lies now.
    [[nodiscard]] virtual TargetState state() const = 0;
    [[nodiscard]] virtual const combat::Health& health() const = 0;
    /// A hit this update, acted on at its next update (the update keeps its largest).
    virtual void hit(const IncomingHit& hit) = 0;
    /// The attacker's clip announces its hit (message `0xa4` or `0xa6`), acted on at its next update.
    virtual void warn(const AttackNotice& notice) = 0;
    /// A human standing at `attacker` started an attack (event `0x10`, sent by `Attack_Start` and the square path,
    /// docs/research/ai.md#block); a brain counts the ones its range and field of view take in. Nothing by default.
    virtual void announceAttack(anim::Vec3 /*attacker*/) {}
    /// The passive target behind this view, which a grab or a tackle can hold; null for a human that cannot be held
    /// yet (**Coney choice**: a grab between two humans is not built, so only the sandbox's targets are grabbed).
    [[nodiscard]] virtual TargetHuman* passive() { return nullptr; }
};

} // namespace coney::human
