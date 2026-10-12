// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/threats.h"

#include "ai/brain.h"
#include "ai/gangs.h"

namespace coney::ai {

namespace {

// Whether `brain`'s human is driven by a pad (a player's).
bool padPlayer(const Brain& brain) { return brain.type() == BrainType::Player; }

// Whether `brain` is a Warrior hitting back at a player (`+0x2e5`).
bool warriorHittingBack(const Brain& brain) { return brain.type() == BrainType::Warrior && brain.hittingBack(); }

} // namespace

bool isThreat(const Brain& brain, const Brain& other) {
    // 1. Never himself.
    if (&brain == &other) {
        return false;
    }
    // 2. A Warrior hitting back and any pad player, either way round.
    if ((warriorHittingBack(brain) && padPlayer(other)) || (warriorHittingBack(other) && padPlayer(brain))) {
        return true;
    }
    // 3. Enemy gangs (brain's mask only), or two pad players.
    return Gangs::enemies(brain.gang(), other.gang()) || (padPlayer(brain) && padPlayer(other));
}

} // namespace coney::ai
