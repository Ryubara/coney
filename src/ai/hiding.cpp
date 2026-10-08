// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/hiding.h"

#include <algorithm>

#include "ai/perception.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// Whether `hunter` hunts `hider`: it is another fightable brain with him as its target or one of its enemies.
bool hunts(const Brain& hunter, const Brain& hider) {
    if (&hunter == &hider || !Brain::fightable(hunter)) {
        return false;
    }
    return hunter.target() == &hider || std::ranges::find(hunter.enemies(), &hider) != hunter.enemies().end();
}

// The far hunters with no sight of `hider` stop counting (`Brain_ShakeOffPursuers`).
void shakeOff(const Brains& brains, const Brain& hider, HideMemory& memory) {
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const Brain& hunter = brains.at(i);
        if (!hunts(hunter, hider) || hunter.distanceTo(hider) <= 2.0F * hunter.meleeFar()) {
            continue;
        }
        if (!lineOfSight(brains.collision(), hunter.human().position(), hider.human().position()).clear) {
            memory.shakenOff.push_back(&hunter);
        }
    }
}

} // namespace

int huntersOf(const Brains& brains, const Brain& hider, const HideMemory& memory) {
    int count = 0;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const Brain& hunter = brains.at(i);
        if (hunts(hunter, hider) && std::ranges::find(memory.shakenOff, &hunter) == memory.shakenOff.end()) {
            ++count;
        }
    }
    return count;
}

HideResult updateHiding(const Brains& brains, Brain& hider, HideMemory& memory, bool carryingMolotov) {
    human::Human& human = hider.human();
    // Off shadow ground: the memory goes and a hidden human leaves the state (with its grace when it applies).
    if (!human.onShadowGround()) {
        memory = HideMemory{};
        human.leaveHiding();
        return HideResult{};
    }
    // Stepping onto it shakes off the far hunters who cannot see him.
    if (!memory.onShadow) {
        shakeOff(brains, hider, memory);
        memory.onShadow = true;
    }
    // Not hunted and allowed: hidden (already hidden, he stays so); otherwise he leaves the state.
    if (!carryingMolotov && huntersOf(brains, hider, memory) == 0) {
        human.enterHiding();
        return HideResult{.mayHide = true};
    }
    human.leaveHiding();
    return HideResult{};
}

} // namespace coney::ai
