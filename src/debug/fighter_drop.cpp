// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/fighter_drop.h"

#include <cstddef>
#include <numbers>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "animation/anim_math.h"
#include "human/human.h"

namespace coney::debug {

// @coney-only a debug aid: the original has no way to drop a human
ai::Brain* dropTarget(ai::Brains& brains, const ai::Brain& player, std::optional<double> handle) {
    const anim::Vec3 from = player.human().position();
    ai::Brain* best = nullptr;
    float bestDistance = 0.0F;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        ai::Brain& brain = brains.at(i);
        // Only an AI human still in the world.
        if (&brain == &player || brain.type() == ai::BrainType::Player || brain.human().outOfWorld()) {
            continue;
        }
        if (handle) {
            if (brain.handle() == *handle) {
                return &brain;
            }
            continue;
        }
        // The nearest is a foe or a bystander, never one of the player's own (a level's crew).
        if (ai::Gangs::friends(brain.gang(), player.gang())) {
            continue;
        }
        const anim::Vec3 at = brain.human().position();
        const float dx = at.x - from.x;
        const float dy = at.y - from.y;
        const float dz = at.z - from.z;
        const float distance = dx * dx + dy * dy + dz * dz;
        if (best == nullptr || distance < bestDistance) {
            best = &brain;
            bestDistance = distance;
        }
    }
    return best;
}

// @coney-only a debug aid: the original has no way to drop a human
void dropBelowWorld(ai::Brain& brain) {
    human::Human& human = brain.human();
    anim::Vec3 position = human.position();
    position.z -= kDropDepth;
    human.place(nullptr, position, human.heading() * 180.0F / std::numbers::pi_v<float>);
    brain.placed();
}

} // namespace coney::debug
