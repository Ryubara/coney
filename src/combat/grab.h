// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "animation/anim_math.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/meters.h"
#include "combat/stick.h"

// Grabbing and tackling: when circle may start one, how far it searches for a target, and what the buttons and the
// stick do while the player holds someone: strikes, the power strike, throws by stick direction, the mugging and the
// spin, with what each costs from the power meter.
// Research: docs/research/combat.md#grab, docs/research/combat.md#grabbing, docs/research/combat.md#throws

namespace coney::combat {

/// The record `+0x08` bits that refuse a grab or a tackle (any attack phase among them).
inline constexpr std::uint32_t kGrabRefusingPhases = 0x0fc7eaf7;
/// The stick length beyond which circle in a grab throws rather than spins.
inline constexpr float kThrowStick = 0.25F;
/// A player pays this share of a grab strike's cost (`0x00510998` is halved for a player).
inline constexpr float kPlayerStrikeCostShare = 0.5F;

/// Circle tapped grabs; circle held tackles.
enum class GrabKind : std::uint8_t { Grab, Tackle };

/// Whether a grab or tackle may start with the record's `+0x08` at `phaseFlags`.
[[nodiscard]] bool grabAllowed(std::uint32_t phaseFlags);

/// How far the search for a target reaches: the far range of anim 70 `GRAB_INTRO` for a grab (2.499 m for the player,
/// so 3.12 m) or of anim 3 for a tackle (2.999 m, so 3.75 m), times `scale` (CombatTuning::grabSearchScale).
[[nodiscard]] float grabSearchRange(const AnimRangeList& ranges, GrabKind kind, float scale);

/// One human the search may pick.
struct TargetCandidate {
    anim::Vec3 position;   ///< Where it stands.
    bool available = true; ///< False for one the search must skip (dead, already held, not a fighter).
};

/// "No target found".
inline constexpr std::size_t kNoTarget = std::numeric_limits<std::size_t>::max();

/// The index of the nearest available candidate within `range` of `from`, or kNoTarget.
/// **Coney choice**: the nearest by straight-line distance, with no facing cone; the original's search routine
/// (`0x0027ac30`) is not researched beyond its range.
[[nodiscard]] std::size_t nearestTarget(const anim::Vec3& from, std::span<const TargetCandidate> candidates,
                                        float range);

/// The throw for a stick on `side` of the player: the THROW_01 set (147 front, 149 right, 151 rear, 153 left), or the
/// THROW_02 set (155, 157, 159, 161) with a wall within reach.
/// @orig 0x0026dd08 Player_Throw (unknown)
[[nodiscard]] int throwAttack(Side side, bool wallInReach);

/// What the player did in a grab this update.
enum class GrabAction : std::uint8_t {
    None,        ///< Nothing.
    Strike,      ///< Square (51 or 53) or cross (55).
    PowerStrike, ///< Cross held and square pressed (57, 63 in rage), or circle + cross (63).
    Throw,       ///< Circle with the stick beyond kThrowStick.
    Mug,         ///< Triangle at a victim that can be mugged: the mugging starts.
    Spin,        ///< R1 pressed, or circle without the stick from the rear: the hold turns front to rear or back.
    Release,     ///< A power strike or throw asked for with too little power: the grab is released.
    LetGo,       ///< L2 held, or the power meter ran out: the player lets go (95, victim 94).
};

/// What a grab update needs to know.
struct GrabInput {
    CommandId command = command::kNone;
    Stick stick;                 ///< In the player's frame.
    bool fromRear = false;       ///< Holding the victim from behind.
    bool raging = false;         ///< The player is raging.
    bool wallInReach = false;    ///< A wall is within the throw's reach.
    bool victimMuggable = false; ///< The victim qualifies for a mugging (`0x00225ff0`).
    bool victimInPlace = true;   ///< The victim stands within 0.3 m of the move's point (`Pair_CheckPlace`).
};

/// What a grab update did.
struct GrabOutcome {
    GrabAction action = GrabAction::None;
    int animId = anim_id::kNone; ///< The player's clip it starts (the spin's 78 or 80, the let-go's 95);
                                 ///< anim_id::kNone for the mugging (its clips are the mugging's) and the release.
    bool spinFirst = false;      ///< A power strike from the rear: the spin to the front (80) plays first.
    int powerSpent = 0;          ///< Taken from the power meter.
};

/// One update of a grab:
/// - square strikes with 51 or 53 at random, cross (its 0x10) with 55, each spending CombatTuning::grabStrikeCost ×
///   0.5 of the meter (40 of 400) at any level;
/// - cross held and square pressed (0x22) is the power strike 57 (63 in rage), circle + cross (0x23) is 63; from the
///   rear the spin to the front plays first;
/// - circle (its press, 0x1e) with the stick beyond kThrowStick throws by the stick's side; without the stick, from
///   the rear, it spins to the front;
/// - the power strikes and the throws need more than CombatTuning::powerEndurance of the meter and spend it (100 of
///   400); with that much or less the grab is released instead;
/// - R1 pressed (3) spins front to rear (78) or rear to front (80); L2 held (5) lets go (95);
/// - triangle mugs a victim that qualifies;
/// - a strike, power strike or throw is refused (nothing played or spent) when the victim is not in its place
///   (`0x00277958`, docs/research/combat.md#grab-posing).
///
/// In rage nothing is spent or needed.
/// **Coney choices**: circle without the stick from the front does nothing (its routine `0x0026f008` is not traced);
/// cross strikes on 0x10, not on its press (which would fire before a power strike's square).
/// @orig 0x0027f3b0 Player_UpdateGrabbing (unknown)
[[nodiscard]] GrabOutcome updateGrab(const GrabInput& input, PowerMeter& power, const CombatTuning& tuning,
                                     CombatRandom& random);

} // namespace coney::combat
