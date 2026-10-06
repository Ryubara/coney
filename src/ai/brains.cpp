// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brains.h"

#include <algorithm>
#include <limits>
#include <span>

namespace coney::ai {

Brain& Brains::add(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed) {
    m_brains.push_back(std::make_unique<Brain>(human, type, settings, seed));
    return *m_brains.back();
}

void Brains::remove(const human::Human& human) {
    const auto found =
        std::ranges::find_if(m_brains, [&human](const std::unique_ptr<Brain>& b) { return &b->human() == &human; });
    if (found == m_brains.end()) {
        return;
    }
    for (const std::unique_ptr<Brain>& other : m_brains) {
        other->forget(**found);
    }
    m_brains.erase(found);
}

Brain* Brains::find(const human::Human& human) {
    const auto found =
        std::ranges::find_if(m_brains, [&human](const std::unique_ptr<Brain>& b) { return &b->human() == &human; });
    return found == m_brains.end() ? nullptr : found->get();
}

void Brains::update() {
    ++m_steps;
    const std::uint64_t now = nowMs();
    for (std::size_t index = 0; index < m_brains.size(); ++index) {
        Brain& brain = *m_brains[index];
        if (!brain.enabled() || brain.human().outOfWorld()) {
            continue;
        }
        if (index % kThinkPeriod == m_steps % kThinkPeriod) {
            brain.think(now);
        }
        brain.update(now);
    }
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        if (brain->type() == BrainType::Player) {
            keepBooks(*brain);
        }
    }
}

human::Humans::BrainsHook Brains::hook() {
    return [this](std::span<human::Human* const> /*humans*/) { update(); };
}

void Brains::keepBooks(Brain& player) {
    for (const std::unique_ptr<Brain>& other : m_brains) {
        if (other->target() == &player) {
            player.addEnemy(*other);
        }
    }
}

std::optional<float> Brains::nearestEnemy(const Brain& brain) {
    std::optional<float> nearest;
    for (const Brain* enemy : brain.enemies()) {
        if (!Brain::fightable(*enemy)) {
            continue;
        }
        const float distance = brain.distanceTo(*enemy);
        nearest = std::min(nearest.value_or(std::numeric_limits<float>::max()), distance);
    }
    return nearest;
}

} // namespace coney::ai
