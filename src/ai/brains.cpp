// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brains.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "combat/meters.h"

namespace coney::ai {

namespace {

// The health the damage stand-in measures falls in: 0 once it has run out. The death chain then leaves a downed human 1
// (Human_PlayDeathReaction 0x00267a00, docs/research/combat-reactions.md#down-health), which is no health given back,
// so a lethal hit reports all the health it took, never one point less.
// @coney-only bookkeeping for the reportDamage() stand-in
int healthSeen(const human::Human& human) {
    const combat::Health& health = human.fighter().health();
    return health.depleted() ? 0 : health.value();
}

} // namespace

// The gangs' tactic rolls start from their usual seed, 1, moved by the variation.
Brains::Brains(std::uint32_t variation) : m_gangs(variedSeed(1, variation)), m_variation(variation) {}

Brains::~Brains() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        human::Human& human = brain->human();
        if (linked(human) && human.brainSlot() == brain->slot()) {
            human.setBrainSlot(std::nullopt);
        }
    }
}

Brain& Brains::add(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed) {
    // The first brain a human was given is the one find() gives, as the scan in slot order did: only a human with no
    // brain here yet is linked to the new slot, or, when another table holds his slot, listed as not linked.
    const bool first = find(human) == nullptr;
    // The flags its brain type gives a human as it is made (Human_Init), on top of what it has.
    human.setFlag(startingFlags(type), true);
    m_brains.push_back(std::make_unique<Brain>(human, type, settings, variedSeed(seed, m_variation), nowMs()));
    m_brains.back()->setSlot(m_brains.size() - 1);
    m_brains.back()->setPlanner(m_planner);
    m_brains.back()->setCollision(m_collision);
    m_brains.back()->setPeers(&m_brains);
    m_brains.back()->setEngineLimits(m_limits);
    m_humanOf.push_back(&human);
    if (first && !human.brainSlot().has_value()) {
        human.setBrainSlot(static_cast<std::uint32_t>(m_brains.size() - 1));
    } else if (first) {
        m_unlinked.push_back(&human);
    }
    return *m_brains.back();
}

void Brains::remove(const human::Human& human) {
    const auto found =
        std::ranges::find_if(m_brains, [&human](const std::unique_ptr<Brain>& b) { return &b->human() == &human; });
    if (found == m_brains.end()) {
        return;
    }
    human::Human& owner = (*found)->human();
    const bool ownerLinked = linked(owner);
    m_gangs.removeMember(**found);
    m_gangs.forget(**found);
    m_formations.forget(**found);
    for (const std::unique_ptr<Brain>& other : m_brains) {
        other->forget(**found);
    }
    m_humanOf.erase(m_humanOf.begin() + (found - m_brains.begin()));
    m_brains.erase(found);
    // The slots after it close up; a human whose slot was a brain's old one follows it to the new.
    for (std::size_t slot = 0; slot < m_brains.size(); ++slot) {
        Brain& brain = *m_brains[slot];
        human::Human& other = brain.human();
        if (linked(other) && other.brainSlot() == brain.slot()) {
            other.setBrainSlot(static_cast<std::uint32_t>(slot));
        }
        brain.setSlot(slot);
    }
    // The human's next brain in slot order, if he had two, is found from now on.
    const std::optional<std::size_t> next = firstSlotOf(owner);
    if (ownerLinked) {
        owner.setBrainSlot(next.has_value() ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(*next))
                                            : std::nullopt);
    } else if (!next.has_value()) {
        std::erase(m_unlinked, &owner);
    }
}

Brain* Brains::find(const human::Human& human) {
    // A human another table holds the slot of is found by the walk; every other one by his slot, checked to be his.
    if (!linked(human)) {
        const std::optional<std::size_t> slot = firstSlotOf(human);
        return slot.has_value() ? m_brains[*slot].get() : nullptr;
    }
    const std::optional<std::uint32_t> slot = human.brainSlot();
    if (slot.has_value() && *slot < m_humanOf.size() && m_humanOf[*slot] == &human) {
        return m_brains[*slot].get();
    }
    return nullptr;
}

bool Brains::linked(const human::Human& human) const {
    return m_unlinked.empty() || std::ranges::find(m_unlinked, &human) == m_unlinked.end();
}

std::optional<std::size_t> Brains::firstSlotOf(const human::Human& human) const {
    const auto found = std::ranges::find(m_humanOf, &human);
    return found == m_humanOf.end() ? std::nullopt
                                    : std::optional<std::size_t>(static_cast<std::size_t>(found - m_humanOf.begin()));
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

void Brains::setEngineLimits(EngineLimits limits) {
    m_limits = limits;
    m_gangs.setEngineLimits(limits);
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        brain->setEngineLimits(limits);
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
        const int health = healthSeen(brain->human());
        const int seen = brain->seenHealth();
        if (seen >= 0 && health < seen) {
            brain->setUnreportedDamage(brain->unreportedDamage() + seen - health);
            brain->setSeenHealth(health);
        }
    }
}

void Brains::reportDamage() {
    for (const std::unique_ptr<Brain>& brain : m_brains) {
        const int health = healthSeen(brain->human());
        const int seen = brain->seenHealth();
        brain->setSeenHealth(health);
        // What noteDamage() kept from before the tactics ran, and whatever fell since.
        const int damage = brain->unreportedDamage() + (seen >= 0 && health < seen ? seen - health : 0);
        brain->setUnreportedDamage(0);
        // The hit timer as the last look saw it, before any hit since: the original sends event 1 at the strike, before
        // the victim's update writes record +0x108 (Human_DealDamage 0x0021d680), so its busy tests see only an earlier
        // hit's timer.
        human::Fighter& fighter = brain->human().fighter();
        const std::uint64_t timerAtStrike = brain->seenBusyUntilMs();
        brain->setSeenBusyUntilMs(fighter.busyUntilMs());
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
        fighter.readBusyUntilAs(timerAtStrike);
        deliverEvent(*brain, BrainEvent{.id = kEventDamaged, .other = nearest, .value = damage});
        fighter.readBusyUntilAs(std::nullopt);
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
