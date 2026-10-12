// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/tandem.h"

#include <algorithm>
#include <cstddef>

namespace coney::combat {

namespace {

// The held objects' anim sets that pick a set of their own (docs/research/combat.md#bat): 1 the knife, 2 the baton,
// 3 the bat.
constexpr int kKnifeSet = 1;
constexpr int kBatonSet = 2;
constexpr int kBatSet = 3;
// Each set is six ids long, the empty-handed three one after the other.
constexpr int kSetLength = 6;
constexpr int kUnarmedSets = 3;

} // namespace

bool tandemCanStart(const TandemCheck& check) {
    return check.victimRearHeld && check.attackerSide == Side::Front && (check.victimHeld & kTandemRefusingHeld) == 0;
}

int tandemSet(int heldSet, CombatRandom& random) {
    if (heldSet == kBatonSet || heldSet == kBatSet) {
        return kBatTandem;
    }
    if (heldSet == kKnifeSet) {
        return kKnifeTandem;
    }
    // One of the three at random. **Coney's reading**: the original's draw is not on the page; each is equally likely.
    const int pick = std::min(kUnarmedSets - 1, static_cast<int>(random.unit() * static_cast<float>(kUnarmedSets)));
    return kTandem1 + (pick * kSetLength);
}

int tandemDamage(const AnimRangeList& ranges, int base) {
    const TandemClips clips = tandemClips(base);
    return ranges.damage(static_cast<std::size_t>(clips.attackerIntro)) +
           ranges.damage(static_cast<std::size_t>(clips.attackerEnd));
}

bool damageDefeats(const DefeatTest& test) {
    if (test.god || test.demiGod || test.knockedOut) {
        return false;
    }
    constexpr int kCuffedFactor = 3;
    const int damage = test.cuffed ? test.damage * kCuffedFactor : test.damage;
    return test.health <= damage;
}

} // namespace coney::combat
