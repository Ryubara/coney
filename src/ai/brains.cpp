// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brains.h"

#include <algorithm>
#include <limits>
#include <span>

namespace coney::ai {

Brain& Brains::add(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed) {
    m_brains.push_back(std::make_unique<Brain>(human, type, settings, seed));
    m_brains.back()->setSlot(m_brains.size() - 1);
    m_brains.back()->setPlanner(m_planner);
    m_brains.back()->setCollision(m_collision);
    return *m_brains.back();
}

void Brains::remove(const human::Human& human) {
    const auto found =
        std::ranges::find_if(m_brains, [&human](const std::unique_ptr<Brain>& b) { return &b->human() == &human; });
    if (found == m_brains.end()) {
        return;
    }
    m_gangs.removeMember(**found);
    m_gangs.forget(**found);
    m_formations.forget(**found);
    for (const std::unique_ptr<Brain>& other : m_brains) {
        other->forget(**found);
    }
    m_brains.erase(found);
    for (std::size_t slot = 0; slot < m_brains.size(); ++slot) {
        m_brains[slot]->setSlot(slot);
    }
}

Brain* Brains::find(const human::Human& human) {
    const auto found =
        std::ranges::find_if(m_brains, [&human](const std::unique_ptr<Brain>& b) { return &b->human() == &human; });
    return found == m_brains.end() ? nullptr : found->get();
}

void Brains::setPlanner(RoutePlanner* planner) {
    m_planner = planner;
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        brain->setPlanner(planner);
    }
}

void Brains::setCollision(const raycast::CollisionMesh* collision) {
    m_collision = collision;
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        brain->setCollision(collision);
    }
}

void Brains::update() {
    ++m_steps;
    const std::uint64_t now = nowMs();
    m_formations.update(now);
    m_gangs.update(now);
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
    reportDamage();
    reportDowns();
}

void Brains::reportDamage() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        const int health = brain->human().fighter().health().value();
        const int seen = brain->seenHealth();
        brain->setSeenHealth(health);
        if (seen < 0 || health >= seen) {
            continue;
        }
        // The attacker is not passed down with the hit, so the nearest human that can fight stands in for it.
        Brain* nearest = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const std::unique_ptr<Brain>& other : m_brains) {
            if (other.get() == brain.get() || !Brain::fightable(*other)) {
                continue;
            }
            const float distance = brain->distanceTo(*other);
            if (distance < best) {
                best = distance;
                nearest = other.get();
            }
        }
        deliverEvent(*brain, BrainEvent{.id = kEventDamaged, .other = nearest, .value = seen - health});
    }
}

void Brains::reportDowns() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        const bool down = brain->human().fighter().health().depleted();
        if (down && !brain->downReported()) {
            brain->setDownReported(true);
            deliverEvent(*brain, BrainEvent{.id = kEventDown});
        } else if (!down) {
            brain->setDownReported(false);
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
