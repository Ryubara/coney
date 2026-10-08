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

class Holdable;

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
    /// Whether an attacker's target search may pick it (`0x00279410`): not for a human with flag `0x100000000000`
    /// (`HuSetNoTarget`) or whose gang was made untargetable (brain `+0x120`, `GangSetTargetable`). True by default.
    [[nodiscard]] virtual bool targetable() const { return true; }
    /// The human a grab or a tackle can hold behind this view (human/holdable.h); null for one that cannot be held (a
    /// human with flag `0x40`, `HuSetUngrabbable`).
    [[nodiscard]] virtual Holdable* holdable() { return nullptr; }
    /// Whether an attacker's strike shapes can meet it (a body with its own spine and head shapes, posed each update,
    /// human/strike_shapes.h): its hits then land only where a shape touches it. False by default: a target without
    /// shapes takes an attack's hit at its hit update (combat::attackHitUpdate(), a **Coney stand-in**).
    [[nodiscard]] virtual bool struckByShapes() const { return false; }
    /// Where an attack's led steer aims (slot point 0, `Human_GetLedSlotPoint(target, 0)` `0x00226aa0`): a human's head
    /// (bone 6) in the world, a few centimetres off its position at rest and up to 0.28 m in a reel
    /// (docs/research/combat.md#led-steer). The position by default.
    [[nodiscard]] virtual anim::Vec3 ledPoint() const { return position(); }
};

} // namespace coney::human
