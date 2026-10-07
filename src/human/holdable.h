// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "animation/anim_math.h"
#include "characters/anim_set.h"
#include "human/combatant.h"
#include "human/human_animator.h"

// A human another human's grab or tackle can hold: the sandbox's passive target (TargetHuman) or a human with a brain
// or a pad (Human). The grabber plays the victim's side of each paired move on it, aligns it, and while the hold is
// attached places it every update at the hold's offset in the grabber's frame (the original's `Human_MoveAttached`,
// `0x00244e78`); the victim's own update then neither moves it nor lets it act.
// Research: docs/research/combat.md#grab, docs/research/combat.md#grab-posing, docs/research/combat.md#grab-turn

namespace coney::human {

/// An AI's counter press (command 3) as its grabber or tackler takes it: what each counter does to the attacker, from
/// the counterer's Anim Range List (docs/research/ai.md#block).
struct CounterPress {
    int grabDamage = 0;   ///< 76 `GRAB_FRONT_COUNTER`'s damage.
    int tackleDamage = 0; ///< 9 `TACKLE_FRONT_COUNTER`'s damage.
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
    /// Turns it to face `point`.
    virtual void face(anim::Vec3 point) = 0;
    /// Its own anim set (an escape's grabber side plays from it).
    [[nodiscard]] virtual const characters::AnimSet& anims() const = 0;
    /// Its AI counter press (command 3) of its last update, made while it could counter (Fighter's test), once:
    /// the grabber whose intro it answers takes it. None for one no brain drives (the default).
    [[nodiscard]] virtual std::optional<CounterPress> takeCounterPress() { return std::nullopt; }
};

} // namespace coney::human
