// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/anim_ranges.h"
#include "combat/reactions.h"
#include "combat/stick.h"

// The tandem's rules as numbers and yes-or-no answers, kept apart from the humans so they can be tested on their own:
// whether an attacker may start one at a victim another human holds from behind (`Tandem_CanStart`), which of the five
// sets of six clips it plays, the damage the victim takes at the end of his intro, and whether that damage defeats him
// (`Human_IsDamageDefeating`). The three humans' clips, places and timing are human::Fighter's (fighter_tandem.cpp).
// Any fighter's dispatcher asks these, a player's or an AI's: square, cross, circle tapped and circle held all test the
// tandem (combat::PlayerCombat), so an AI that presses circle at a rear-held enemy it faces starts one too (#84).
// Research: docs/research/combat-moves.md#tandem, docs/research/combat-moves.md#tandem-ai

namespace coney::combat {

/// The victim's record `+0x08` bits that refuse a tandem: `0x800000`, `0x400000` (a paired move or a tandem under way),
/// `0x10000`, `0x2000` and `0x200`.
inline constexpr std::uint32_t kTandemRefusingHeld = 0xc12200;
/// What the attacker's and the grabber's clips hold on the record `+0x08` for the whole tandem...
inline constexpr std::uint32_t kTandemAttackerHeld = 0x800000;
/// ... and the victim's (docs/research/combat-reactions.md#protected-moves): no one else starts an attack on any of the
/// three, and a hit takes health but plays no reaction.
inline constexpr std::uint32_t kTandemVictimHeld = 0x400000;

/// The first id of each set (docs/research/combat-moves.md#frame-data): `TANDEM_01` to `TANDEM_03` empty-handed,
/// `BAT_TANDEM_01` with a bat or a baton, `KNIFE_TANDEM_01` with a knife.
inline constexpr int kTandem1 = 163;
inline constexpr int kTandem2 = 169;
inline constexpr int kTandem3 = 175;
inline constexpr int kBatTandem = 181;
inline constexpr int kKnifeTandem = 187;

/// What `Tandem_CanStart` reads of the victim and where the attacker stands.
struct TandemCheck {
    /// The victim is in state `0x20`: held in a grab from his rear, by anyone (a gang member, an AI, another player).
    bool victimRearHeld = false;
    /// The attacker's side of the victim (victimSide(), `Human_GetSideOf(victim, attacker, 1)`).
    Side attackerSide = Side::Rear;
    /// The victim's record `+0x08`.
    std::uint32_t victimHeld = 0;
};

/// Whether a tandem may start: the victim held from the rear, the attacker within 45° of his front (Side::Front) and
/// none of kTandemRefusingHeld on the victim.
/// @orig 0x00258e10 Tandem_CanStart (unknown)
[[nodiscard]] bool tandemCanStart(const TandemCheck& check);

/// The set's first id for an attacker holding an object of anim set `heldSet` (0 for none): a baton (2) or a bat (3)
/// kBatTandem, a knife (1) kKnifeTandem, anything else kTandem1, kTandem2 or kTandem3, one of the three drawn evenly
/// from `random` (**Coney's choice**: the original's draw is not on the page,
/// docs/research/combat-moves.md#tandem-coney).
[[nodiscard]] int tandemSet(int heldSet, CombatRandom& random);

/// The six clips of the set that starts at `base`: an intro and an end for each of the three, ids base + 0 to + 5.
struct TandemClips {
    int grabberIntro = 0;  ///< base + 0
    int victimIntro = 0;   ///< base + 1
    int attackerIntro = 0; ///< base + 2
    int grabberEnd = 0;    ///< base + 3
    int victimEnd = 0;     ///< base + 4
    int attackerEnd = 0;   ///< base + 5
};

/// The six clips of the set starting at `base`.
[[nodiscard]] constexpr TandemClips tandemClips(int base) {
    return TandemClips{.grabberIntro = base,
                       .victimIntro = base + 1,
                       .attackerIntro = base + 2,
                       .grabberEnd = base + 3,
                       .victimEnd = base + 4,
                       .attackerEnd = base + 5};
}

/// The damage the victim takes at the end of his intro: the attacker's intro and end values in `ranges` (50 + 350
/// empty-handed, 50 + 500 with a bat, 50 + 600 with a knife on the disc).
[[nodiscard]] int tandemDamage(const AnimRangeList& ranges, int base);

/// What `Human_IsDamageDefeating` reads of the human.
struct DefeatTest {
    int health = 0;          ///< Its health (`+0x144`).
    int damage = 0;          ///< The damage to test.
    bool cuffed = false;     ///< Cuffed: the damage counts three times over.
    bool god = false;        ///< Human flag `0x10`.
    bool demiGod = false;    ///< Human flag `0x20000000000` (the health floor).
    bool knockedOut = false; ///< Knocked out (state `0x100000000`).
};

/// Whether `damage` defeats the human: never for a god, a demi-god or one knocked out; otherwise when its health is at
/// most the damage (tripled when cuffed).
/// @orig 0x00265c28 Human_IsDamageDefeating (unknown)
[[nodiscard]] bool damageDefeats(const DefeatTest& test);

} // namespace coney::combat
