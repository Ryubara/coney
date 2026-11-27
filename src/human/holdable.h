// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "animation/anim_math.h"
#include "characters/anim_set.h"
#include "combat/grabbed.h"
#include "combat/tackled.h"
#include "human/combatant.h"
#include "human/human_animator.h"

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

// A human another human's grab or tackle can hold: the sandbox's passive target (TargetHuman) or a human with a brain
// or a pad (Human). The grabber plays the victim's side of each paired move on it, aligns it, and while the hold is
// attached places it every update at the hold's offset in the grabber's frame (the original's `Human_MoveAttached`,
// `0x00244e78`); the victim's own update then neither moves it nor lets it act.
// Research: docs/research/combat-grabs.md#grab, docs/research/combat-grabs.md#grab-posing,
// docs/research/combat-grabs.md#grab-turn

namespace coney::human {

/// An AI's counter press (command 3) as its grabber or tackler takes it: what each counter does to the attacker, from
/// the counterer's Anim Range List (docs/research/ai-fights.md#block).
struct CounterPress {
    int grabDamage = 0;   ///< 76 `GRAB_FRONT_COUNTER`'s damage.
    int tackleDamage = 0; ///< 9 `TACKLE_FRONT_COUNTER`'s damage.
};

/// What the grabbed human did to its grabber in an update, for the grabber to apply.
struct GrabbedReport {
    combat::GrabbedAction action = combat::GrabbedAction::None;
    bool countered = false;          ///< R1 at the catch: the player's counter 76 plays instead of the grab.
    int victimClip = -1;             ///< The grabbed human's move (96 / 108, 104 / 116, 100 / 112).
    int grabberClip = -1;            ///< The clip the grabber plays (the move's id + 1, from the grabbed human's set).
    int grabberPowerCost = 0;        ///< Power the grabber loses.
    int grabberDamage = 0;           ///< Damage the grabber takes (the move's Anim Range List damage).
    bool grabberKnockedDown = false; ///< The grabber goes down (an escape)...
    bool grabberStunned = false;     ///< ... and is stunned (an escape, a counter).
    bool ended = false;              ///< The grabbed human is out of the grab.
    /// Mounted: what it did from under the mount (`Player_UpdateTackled`); `victimClip`, `grabberClip`,
    /// `grabberPowerCost` and `grabberDamage` are the struggle's.
    combat::TackledAction mount = combat::TackledAction::None;
};

/// What a tandem's attacker tells its victim as the tandem starts (Holdable::startTandem(),
/// docs/research/combat-moves.md#tandem).
struct TandemStart {
    int base = -1;                                      ///< The set's first id (combat::tandemSet()).
    const characters::AnimSet* attackerAnims = nullptr; ///< The victim plays its clips from the attacker's set.
    int damage = 0;                                     ///< combat::tandemDamage(): taken when his intro ends.
    anim::Vec3 attacker;                                ///< Where the attacker's feet are.
    bool attackerIsPlayer = false;                      ///< A player's tandem.
};

/// A paired move an attacker starts on a standing human, as the stealth kill does (`Attack_StartPaired` `0x00262ac8`
/// on a free victim, Holdable::takePairedMove(), docs/research/stealth.md#stealth-kill).
struct PairedMove {
    int attackAnim = -1;                                ///< The attacker's clip; the victim plays the next id.
    const characters::AnimSet* attackerAnims = nullptr; ///< The victim plays its side from the attacker's set.
    int damage = 0;                                     ///< Taken at once; a defeat plays the death chain.
    anim::Vec3 attacker;                                ///< Where the attacker's feet are.
    bool attackerIsPlayer = false;                      ///< A player's move.
    /// The record `+0x08` bits its side holds and sets when it lives through it (`0x400000` for the stealth kill).
    std::uint32_t victimHeld = 0;
};

/// A combatant a grab or a tackle can hold.
class Holdable : public Combatant {
  public:
    /// Plays the victim's `clips`, then `loop`, in `state` (AnimState::Attack returns to the idle afterwards,
    /// AnimState::Hold keeps the loop), and takes `targetState`: Held or Mounted while the hold lasts; Standing frees
    /// it; Grounded puts it on the ground (its knockdown, and the rise later). A stun ends.
    virtual void play(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state,
                      TargetState targetState) = 0;
    /// The victim's side of a paired move: `clips` from `attacker`'s anim set at its rates (the grabber's reaction
    /// clips, docs/research/formats/animation.md#paired-tasks), then its own `loop`, as play() does otherwise.
    virtual void playPaired(std::span<const std::uint32_t> clips, const characters::AnimSet& attacker,
                            std::uint32_t loop, AnimState state, TargetState targetState) = 0;
    /// Reports the start of the loop its playing chain ends in now, not when it plays: a grab's connect builds the
    /// victim's hold under its reaction (docs/research/characters-animation.md#anim-callbacks). Nothing for one with no
    /// animation callbacks (the default).
    virtual void announceFollowOn() {}
    /// Attached, the grabber places it each update and nothing else moves it; detached, its clips' root motion does.
    virtual void setAttached(bool attached) = 0;
    [[nodiscard]] virtual bool attached() const = 0;
    /// The grabber still drives the hold: called on each of its updates while it holds this victim. A human held
    /// without it for a few updates frees itself (Fighter::keepHold()).
    virtual void keepHold() = 0;
    /// The hold it was in when a placement broke the pair from its side, once (Fighter::takeBrokenHold()); nothing
    /// otherwise.
    [[nodiscard]] virtual std::optional<TargetState> takeBrokenHold() = 0;
    /// Moves it to `position` facing `headingRadians`.
    virtual void place(anim::Vec3 position, float headingRadians) = 0;
    /// Sweeps it by `displacement` against the level `mesh`, as one body of an attached pair is moved toward its place
    /// (`Human_MoveAttached`, docs/research/combat-grabs.md#grab-posing), and returns where it ends; its heading,
    /// velocity and state are left alone. With no mesh it goes the whole way, and so does a body that meets nothing
    /// (the default: a passive target).
    virtual anim::Vec3 sweepAttached(const raycast::CollisionMesh* /*mesh*/, anim::Vec3 displacement) {
        const anim::Vec3 to = anim::add(position(), displacement);
        place(to, heading());
        return to;
    }
    /// Turns it to face `point`.
    virtual void face(anim::Vec3 point) = 0;
    /// Turns it to `headingRadians` and slides it to `goal` across the ground, each at a constant rate over `seconds`
    /// (TurnAndSlide: `Human_TurnToOver`, `Human_MoveToOver`), on top of its clip's root motion: a front hold's throw
    /// carries its victim round the thrower this way (docs/research/combat-throws.md#throw-direction).
    virtual void carryOver(anim::Vec3 goal, float headingRadians, float seconds) = 0;
    /// Its own anim set (an escape's grabber side plays from it).
    [[nodiscard]] virtual const characters::AnimSet& anims() const = 0;
    /// Its AI counter press (command 3) of its last update, made while it could counter (Fighter's test), once:
    /// the grabber whose intro it answers takes it. None for one no brain drives (the default).
    [[nodiscard]] virtual std::optional<CounterPress> takeCounterPress() { return std::nullopt; }
    /// Its grabber's numbers this update, while a grab (not a tackle) holds it attached: with them its own next
    /// update may struggle, strike back or escape (`Player_UpdateGrabbed`, docs/research/combat-grabs.md#grabbed). A
    /// passive target ignores them (the default).
    virtual void heldInGrab(const combat::GrabberState& /*grabber*/, bool /*fromRear*/) {}
    /// What it did to its grabber in its last update while held, once (none by default): the grabber plays its side.
    [[nodiscard]] virtual GrabbedReport takeGrabbedReport() { return {}; }
    /// Its grabber's side of the hold, told on each of the grabber's updates while it is attached: `rearGrab` while a
    /// grab holds it from its rear (state `0x20`, Combatant::heldFromRear()). A passive target ignores it (the
    /// default).
    virtual void holdSide(bool /*rearGrab*/) {}
    /// A tandem starts on it (`Tandem_Start`, human::Fighter): it plays its intro and end clips from the attacker's
    /// set, takes the damage when its intro ends, and keeps the set's id for its grabber (takeTandemRequest()). False
    /// when it cannot (the default: a passive target plays no tandem).
    virtual bool startTandem(const TandemStart& /*start*/) { return false; }
    /// A paired move starts on it while it stands (PairedMove): it takes the damage at once and plays its side from
    /// the attacker's set, as the dying clip of its death chain when the damage defeats it, else then the ground. False
    /// when it cannot (the default).
    virtual bool takePairedMove(const PairedMove& /*move*/) { return false; }
    /// Its human flag `0x8` (woundable) is cleared, so a later defeat knocks it out and never leaves it wounded: the
    /// stealth kill does so before it tries the pair (`Player_StartStealthKill` `0x00264738`,
    /// docs/research/stealth.md#stealth-kill). Nothing for one with no such flag (the default).
    virtual void loseWoundable() {}
    /// The tandem its grabber joins, once: the set's first id. Its grabber reads it on each of its updates while it
    /// holds it, so the grabber's clips start on the grabber's next update (the same step when the grabber acts after
    /// the attacker). Nothing by default.
    [[nodiscard]] virtual std::optional<int> takeTandemRequest() { return std::nullopt; }
    /// Whether it lies where a circle at it mounts it with the grounded mount (`Human_CanBeGrabbed` 0x002256a0 for a
    /// knocked-down human, docs/research/combat-reactions.md#ground-escape): down in its grounded idle, or less than
    /// half way through its rise. False by default (a passive target is never mounted where it lies).
    [[nodiscard]] virtual bool mountableOnGround() const { return false; }
    /// It reversed the mount `mounter` held it in (`Mount_StartReversal`): it plays 242 and mounts `mounter`, which
    /// plays 243 (Fighter::reverseMount()). Nothing by default.
    virtual void takeReversedMount(Holdable& /*mounter*/) {}
    /// Its command this update, which its mounter reads as a mount's clip ends (`Mount_OnStrikeEnd` reads its
    /// per-player record). None by default (a passive target presses nothing).
    [[nodiscard]] virtual combat::CommandId mountCommand() const { return combat::command::kNone; }
    /// Its left stick's magnitude (0-1), which the same end reads. 0 by default.
    [[nodiscard]] virtual float mountStick() const { return 0.0F; }
};

} // namespace coney::human
