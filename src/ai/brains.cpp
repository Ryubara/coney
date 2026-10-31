// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brains.h"

#include <algorithm>
#include <limits>
#include <span>

namespace coney::ai {

// The gangs' tactic rolls start from their usual seed, 1, moved by the variation.
Brains::Brains(std::uint32_t variation) : m_gangs(variedSeed(1, variation)), m_variation(variation) {}

Brain& Brains::add(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed) {
    // The flags its brain type gives a human as it is made (Human_Init), on top of what it has.
    human.setFlag(startingFlags(type), true);
    m_brains.push_back(std::make_unique<Brain>(human, type, settings, variedSeed(seed, m_variation), nowMs()));
    m_brains.back()->setSlot(m_brains.size() - 1);
    m_brains.back()->setPlanner(m_planner);
    m_brains.back()->setCollision(m_collision);
    m_brains.back()->setPeers(&m_brains);
    // The first brain a human was given is the one find() gives, as the scan in slot order did.
    m_byHuman.try_emplace(&human, m_brains.back().get());
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
    // The human's next brain in slot order, if it had two, is found from now on.
    m_byHuman.erase(&human);
    if (const auto next =
            std::ranges::find_if(m_brains, [&human](const std::unique_ptr<Brain>& b) { return &b->human() == &human; });
        next != m_brains.end()) {
        m_byHuman.emplace(&human, next->get());
    }
    for (std::size_t slot = 0; slot < m_brains.size(); ++slot) {
        m_brains[slot]->setSlot(slot);
    }
}

Brain* Brains::find(const human::Human& human) {
    const auto found = m_byHuman.find(&human);
    return found == m_byHuman.end() ? nullptr : found->second;
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
    noteDamage();
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
    reportPutDowns();
}

void Brains::noteDamage() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        const int health = brain->human().fighter().health().value();
        const int seen = brain->seenHealth();
        if (seen >= 0 && health < seen) {
            brain->setUnreportedDamage(brain->unreportedDamage() + seen - health);
            brain->setSeenHealth(health);
        }
    }
}

void Brains::reportDamage() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        const int health = brain->human().fighter().health().value();
        const int seen = brain->seenHealth();
        brain->setSeenHealth(health);
        // What noteDamage() kept from before the tactics ran, and whatever fell since.
        const int damage = brain->unreportedDamage() + (seen >= 0 && health < seen ? seen - health : 0);
        brain->setUnreportedDamage(0);
        if (damage <= 0) {
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
        deliverEvent(*brain, BrainEvent{.id = kEventDamaged, .other = nearest, .value = damage});
    }
}

void Brains::reportDowns() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        const bool down = brain->human().fighter().health().depleted();
        if (down && !brain->downReported()) {
            brain->setDownReported(true);
            // Knocked out: his brain lets go of everything and stops, then his human hears of it with his foe; a
            // player's slow motion goes off (`Human_KnockOut`).
            brain->human().endSlowMotion();
            Brain* foe = brain->knockOut();
            deliverEvent(*brain, BrainEvent{.id = kEventDown, .other = foe});
        } else if (!down) {
            brain->setDownReported(false);
            brain->wakeUp();
        }
    }
}

void Brains::reportPutDowns() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        const human::Human& body = brain->human();
        if (body.state() != human::TargetState::Grounded) {
            brain->setLyingReported(false);
            continue;
        }
        const bool lying =
            !body.fighter().health().depleted() && !body.attached() && !body.animator().drivingClipPlaying();
        if (lying && !brain->lyingReported()) {
            brain->setLyingReported(true);
            deliverEvent(*brain, BrainEvent{.id = kEventPutDown, .other = brain.get()});
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
