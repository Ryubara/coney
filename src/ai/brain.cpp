// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "ai/fight_goal.h"
#include "ai/reaction_goals.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// How many times one update processes the goal stack at most: a pop per goal plus a run again each.
constexpr int kMaxGoalPasses = 2 * static_cast<int>(kGoalStackSize);
// A random action delay is 0-500 ms.
constexpr int kRandomDelayMaxMs = 500;

} // namespace

BrainType brainTypeOf(int behaviour) {
    if (behaviour < 0 || behaviour > static_cast<int>(BrainType::CivilianDi)) {
        return BrainType::Gang;
    }
    return static_cast<BrainType>(behaviour);
}

Brain::Brain(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed)
    : m_human(&human), m_type(type), m_settings(settings), m_random(seed) {}

Brain::~Brain() = default;

void Brain::update(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    // The attacks announced since the last update that this brain's range and field of view take in (+0x200).
    m_attackWarnings = 0;
    for (const anim::Vec3 attacker : m_human->takeAttackAnnouncements()) {
        const anim::Vec3 to = anim::subtract(attacker, m_human->position());
        const float distance = std::hypot(to.x, to.y);
        const float off =
            distance > 1e-4F ? std::fabs(human::wrapAngle(human::headingOf(to) - m_human->heading())) : 0.0F;
        if (distance <= m_sightRange && off <= m_fieldOfView) {
            ++m_attackWarnings;
        }
    }
    // The player's brain only keeps books (Brains does them for every player brain at once).
    if (m_type != BrainType::Player && !m_human->airborne() && !m_human->fighter().health().depleted()) {
        // The reaction goal, else the goal stack; then the actions.
        if (!updateReactionGoal()) {
            processGoals();
        }
        runActions();
    }
    m_attackWarnings = 0;
    m_lastUpdateMs = nowMs;
    ++m_updates;
}

void Brain::think(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    ++m_thinks;
}

bool Brain::pushGoal(std::unique_ptr<Goal> goal) {
    if (goal == nullptr || m_goals.size() >= kGoalStackSize) {
        return false;
    }
    if (Goal* top = topGoal(); top != nullptr) {
        top->suspend(*this);
    }
    clearActions();
    m_goals.push_back(std::move(goal));
    return true;
}

void Brain::popGoal() {
    if (m_goals.empty()) {
        return;
    }
    m_goals.back()->end(*this);
    m_goals.pop_back();
    if (Goal* top = topGoal(); top != nullptr) {
        top->m_resumePending = top->m_started;
    } else {
        ++m_goalsRanOut;
    }
}

void Brain::clearGoals() {
    while (!m_goals.empty()) {
        popGoal();
    }
}

Goal* Brain::findGoal(GoalType type) {
    for (auto it = m_goals.rbegin(); it != m_goals.rend(); ++it) {
        if ((*it)->type() == type) {
            return it->get();
        }
    }
    return nullptr;
}

bool Brain::queueAction(std::unique_ptr<Action> action) {
    if (action == nullptr || m_actionCount >= kActionQueueSize) {
        return false;
    }
    m_actions[(m_actionFront + m_actionCount) % kActionQueueSize] = std::move(action);
    ++m_actionCount;
    return true;
}

void Brain::popAction() {
    if (m_actionCount == 0) {
        return;
    }
    m_actions[m_actionFront].reset();
    m_actionFront = (m_actionFront + 1) % kActionQueueSize;
    --m_actionCount;
}

void Brain::clearActions() {
    while (m_actionCount > 0) {
        if (!m_actions[m_actionFront]->abort(*this)) {
            return;
        }
        popAction();
    }
}

Action* Brain::frontAction() { return m_actionCount == 0 ? nullptr : m_actions[m_actionFront].get(); }

void Brain::flush() {
    clearGoals();
    clearActions();
}

void Brain::processGoals() {
    for (int pass = 0; pass < kMaxGoalPasses; ++pass) {
        Goal* top = topGoal();
        if (top == nullptr) {
            return;
        }
        // Started, or taken up again, only with the action queue empty.
        if (!top->m_started || top->m_resumePending) {
            if (m_actionCount > 0) {
                return;
            }
            if (!top->m_started) {
                top->m_started = true;
                top->start(*this);
            } else {
                top->resume(*this);
            }
            top->m_resumePending = false;
        }
        switch (top->process(*this)) {
        case GoalStatus::Done:
            popGoal();
            break;
        case GoalStatus::Stop:
            return;
        case GoalStatus::Again:
            break;
        }
    }
}

bool Brain::updateReactionGoal() {
    if (m_reaction == nullptr) {
        m_reaction = reactionGoalFor(*m_human);
        if (m_reaction == nullptr) {
            return false;
        }
        // **Coney choice**: what the human was doing is over (its clip was cut), so its queued actions go.
        clearActions();
        stopMove();
        m_reaction->m_started = true;
        m_reaction->start(*this);
    }
    if (m_reaction->process(*this) == GoalStatus::Done) {
        m_reaction->end(*this);
        m_reaction.reset();
        if (Goal* top = topGoal(); top != nullptr) {
            top->m_resumePending = top->m_started;
        }
    }
    return true;
}

void Brain::runActions() {
    Action* action = frontAction();
    if (action == nullptr) {
        return;
    }
    if (!action->m_started) {
        // The delay counts down against the time of the last update; a random delay is drawn once.
        if (action->m_delayMs == kRandomDelay) {
            action->m_delayMs = static_cast<std::int16_t>(rollRange(m_random, 0, kRandomDelayMaxMs));
        }
        const auto elapsed = static_cast<int>(std::min<std::uint64_t>(m_nowMs - m_lastUpdateMs, 0x7fff));
        action->m_delayMs = static_cast<std::int16_t>(std::max(0, action->m_delayMs - elapsed));
        if (action->m_delayMs > 0) {
            return;
        }
        action->m_started = true;
        if (action->start(*this) == ActionStatus::Done) {
            popAction();
            return;
        }
    }
    if (action->update(*this) == ActionStatus::Done) {
        popAction();
    }
}

void Brain::startFight(Brain& target) {
    clearActions();
    fight(target);
}

bool Brain::fight(Brain& target) {
    if (m_threatResponse == 0 || &target == this) {
        return false;
    }
    addEnemy(target);
    setTarget(&target);
    // The fight goal, unless one is already on top.
    const Goal* top = topGoal();
    if (top == nullptr || top->type() != GoalType::Fight) {
        pushGoal(std::make_unique<FightGoal>());
    }
    return true;
}

void Brain::addEnemy(Brain& enemy) {
    if (m_enemies.size() >= kMaxEnemies || std::ranges::find(m_enemies, &enemy) != m_enemies.end()) {
        return;
    }
    m_enemies.push_back(&enemy);
}

void Brain::setTarget(Brain* target) {
    if (m_target == target) {
        if (m_target != nullptr) {
            m_target->claimSlot(*this);
        }
        return;
    }
    if (m_target != nullptr) {
        m_target->releaseSlot(*this);
    }
    m_target = target;
    if (m_target != nullptr) {
        m_target->claimSlot(*this);
    }
}

bool Brain::hasAttackSlot() const {
    return m_target != nullptr && std::ranges::find(m_target->m_slots, this) != m_target->m_slots.end();
}

void Brain::retarget() {
    Brain* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (Brain* enemy : m_enemies) {
        if (!fightable(*enemy) || enemy->human().state() != human::TargetState::Standing) {
            continue;
        }
        if (const float distance = distanceTo(*enemy); distance < best) {
            best = distance;
            nearest = enemy;
        }
    }
    if (nearest != nullptr && nearest != m_target) {
        setTarget(nearest);
    }
}

bool Brain::claimSlot(Brain& attacker) {
    if (std::ranges::find(m_slots, &attacker) != m_slots.end()) {
        return true;
    }
    if (m_slots.size() < m_slotCount) {
        m_slots.push_back(&attacker);
        return true;
    }
    // Full: a closer attacker takes the farthest one's slot.
    const auto farthest = std::ranges::max_element(m_slots, {}, [this](const Brain* b) { return distanceTo(*b); });
    if (farthest == m_slots.end() || distanceTo(attacker) >= distanceTo(**farthest)) {
        return false;
    }
    *farthest = &attacker;
    return true;
}

void Brain::releaseSlot(const Brain& attacker) { std::erase(m_slots, &attacker); }

void Brain::setAttackSlotCount(std::size_t count) {
    m_slotCount = std::min(count, kMaxAttackSlots);
    if (m_slots.size() > m_slotCount) {
        m_slots.resize(m_slotCount);
    }
}

void Brain::setMeleeRange(float nearRange, float farRange) {
    m_meleeNear = nearRange;
    m_meleeFar = farRange;
}

void Brain::setSight(float range, float fieldOfView) {
    m_sightRange = range;
    m_fieldOfView = fieldOfView;
}

void Brain::setAttackWeight(int kind, std::uint8_t weight) {
    if (kind >= 0 && kind < static_cast<int>(kAttackKinds)) {
        m_settings.attackWeights[static_cast<std::size_t>(kind)] = weight;
    }
}

int Brain::attackDelayMs(int kind, bool targetDown, bool halved) const {
    if (kind < 0 || kind >= static_cast<int>(kAttackKinds)) {
        return 0;
    }
    const combat::PowerClass& power = m_human->fighterProfile().powerClass;
    const float factor = targetDown ? power.attackDelayDownFactor : power.attackDelayFactor;
    const float delay = static_cast<float>(m_settings.attackDelaysMs[static_cast<std::size_t>(kind)]) * factor;
    return static_cast<int>(halved ? delay * 0.5F : delay);
}

float Brain::blockChance() const {
    const combat::PowerClass& power = m_human->fighterProfile().powerClass;
    const float chance = m_human->fighter().hurt() ? power.hurtBlockChance : power.blockChance;
    return chance * static_cast<float>(m_settings.baseBlockChance);
}

float Brain::counterChance() const { return m_human->fighterProfile().powerClass.counterChance * 100.0F; }

void Brain::press(combat::CommandId command) { m_human->record().command = command; }

void Brain::setMove(anim::Vec3 way, float speed) {
    const float length = std::hypot(way.x, way.y);
    const float heading = length > 1e-4F ? human::headingOf(way) : m_human->heading();
    m_human->record().move = human::BrainMove{.heading = heading, .speed = speed};
}

void Brain::setMoveHeading(float heading, float speed) {
    m_human->record().move = human::BrainMove{.heading = human::wrapAngle(heading), .speed = speed};
}

void Brain::setMoveAim(anim::Vec3 point, float radius) {
    m_moveAim = point;
    m_moveAimRadius = radius;
}

void Brain::stopMove() { m_human->record().move.reset(); }

void Brain::writeStick(anim::Vec3 way, float magnitude) {
    const float length = std::hypot(way.x, way.y);
    human::PlayerRecord& record = m_human->record();
    record.cameraForward = anim::Vec3{0.0F, 1.0F, 0.0F};
    record.stickX = length > 1e-4F ? way.x / length * magnitude : 0.0F;
    record.stickY = length > 1e-4F ? way.y / length * magnitude : 0.0F;
}

void Brain::releaseStick() {
    m_human->record().stickX = 0.0F;
    m_human->record().stickY = 0.0F;
}

float Brain::distanceTo(const Brain& other) const {
    const anim::Vec3 to = anim::subtract(other.human().position(), m_human->position());
    return std::hypot(to.x, to.y);
}

bool Brain::fightable(const Brain& other) { return !other.human().fighter().health().depleted(); }

void Brain::forget(const Brain& other) {
    if (m_target == &other) {
        m_target = nullptr;
    }
    std::erase(m_enemies, &other);
    std::erase(m_slots, &other);
}

} // namespace coney::ai
