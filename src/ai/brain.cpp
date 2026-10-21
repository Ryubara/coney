// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "ai/attack_views.h"
#include "ai/brain_events.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/melee_goal.h"
#include "ai/perception.h"
#include "ai/reaction_goals.h"
#include "ai/script_services.h"
#include "ai/story_goals.h"
#include "ai/swap_prompt.h"
#include "ai/tactic.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// How many times one update processes the goal stack at most: a pop per goal plus a run again each.
constexpr int kMaxGoalPasses = 2 * static_cast<int>(kGoalStackSize);
// A random action delay is 0-500 ms.
constexpr int kRandomDelayMaxMs = 500;

} // namespace

const char* targetCauseName(TargetCause cause) {
    switch (cause) {
    case TargetCause::Pick:
        return "pick";
    case TargetCause::Front:
        return "front";
    case TargetCause::HitBack:
        return "hit_back";
    case TargetCause::Help:
        return "help";
    case TargetCause::Engage:
        return "engage";
    case TargetCause::Displaced:
        return "displaced";
    case TargetCause::Reaction:
        return "reaction";
    case TargetCause::Order:
        return "order";
    case TargetCause::Other:
        break;
    }
    return "other";
}

BrainType brainTypeOf(int behaviour) {
    if (behaviour < 0 || behaviour > static_cast<int>(BrainType::CivilianDi)) {
        return BrainType::Gang;
    }
    return static_cast<BrainType>(behaviour);
}

namespace {

// Human_Init's and Human_ReleasePlayer's bits (docs/research/combat.md#human-flags): a cop's and a dealer's two have
// no name in Coney yet.
constexpr std::uint64_t kCopFlag2000 = 0x2000;
constexpr std::uint64_t kCopFlag40000 = 0x40000;
constexpr std::uint64_t kRevivableFlags = human::flag::kFastClimber | human::flag::kRevivable;

} // namespace

std::uint64_t startingFlags(BrainType type) {
    switch (type) {
    case BrainType::Player:
        return human::flag::kRageAllowed | kRevivableFlags;
    case BrainType::Warrior:
        return kRevivableFlags;
    case BrainType::Cop:
    case BrainType::Dealer:
        return kCopFlag2000 | kCopFlag40000;
    default:
        return 0;
    }
}

std::uint64_t releasedFlags(std::uint64_t flags, BrainType leftAs) {
    flags &= ~(kRevivableFlags | human::flag::kRageAllowed);
    if (leftAs == BrainType::Player || leftAs == BrainType::Warrior) {
        flags |= kRevivableFlags;
    } else if (leftAs == BrainType::Cop) {
        flags |= kCopFlag2000;
    }
    return flags;
}

Brain::Brain(human::Human& human, BrainType type, const FightSettings& settings, std::uint32_t seed)
    : m_human(&human), m_settings(settings), m_random(seed), m_type(type) {
    // The human's damage reads whether its brain is a Warrior's (`Human_AddPendingDamage` 0x00264bd8).
    human.fighter().setWarriorBrain(type == BrainType::Warrior);
}

Brain::~Brain() = default;

// The brain whose human stands at `position` (the announced attacker); null when none does. **Coney reading**: the
// announcement carries the attacker's position, so the attacker is the peer standing there (the boss counters answer
// him, docs/research/ai-gangs.md#boss-diego-vargas).
Brain* Brain::announcer(anim::Vec3 position) const {
    if (m_peers == nullptr) {
        return nullptr;
    }
    constexpr float kSamePlace = 1e-3F;
    for (const std::unique_ptr<Brain>& peer : *m_peers) {
        if (peer.get() != this && anim::distance(peer->human().position(), position) <= kSamePlace) {
            return peer.get();
        }
    }
    return nullptr;
}

void Brain::update(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    // The attacks announced since the last update that this brain's range and field of view take in: events 0x10,
    // which count the warnings (+0x200) unless the gang's tactic takes them or the brain is dead.
    m_attackWarnings = 0;
    for (const anim::Vec3 attacker : m_human->takeAttackAnnouncements()) {
        const anim::Vec3 to = anim::subtract(attacker, m_human->position());
        const float distance = std::hypot(to.x, to.y);
        const float off =
            distance > 1e-4F ? std::fabs(human::wrapAngle(human::headingOf(to) - m_human->heading())) : 0.0F;
        if (distance <= m_sightRange && off <= m_fieldOfView &&
            lineOfSight(m_collision, m_human->position(), attacker).clear) {
            deliverEvent(*this, BrainEvent{.id = kEventAttackWarning, .other = announcer(attacker)});
        }
    }
    // The player's brain only keeps books (Brains does them for every player brain at once), unless it is dead; a
    // suspended brain or gang keeps only the time.
    const bool runsGoals = m_type != BrainType::Player || m_dead;
    const bool held = m_suspended || (m_gang != nullptr && m_gang->suspended());
    // The move is asked for again every update (Brain_StopMove, the first call of Brain_UpdateGoals, even when the
    // rest is skipped): the human stands along its facing unless a goal or an action writes it below
    // (docs/research/characters.md#ai-locomotion). Nothing a finished goal or action wrote outlives its update.
    // @orig 0x0028ac18 Brain_StopMove (unknown)
    if (runsGoals) {
        stopMove();
    }
    if (runsGoals && !held && !m_knockedOut && !m_human->airborne() && !m_human->fighter().health().depleted()) {
        // The enemy scan on this brain's own schedule, before the goals that pick from the list
        // (docs/research/ai-reimpl.md#unit-1). **Coney's reading**: a brain with no gang has no enemy gang to find and
        // takes no scan token.
        if (m_gang != nullptr) {
            static_cast<void>(m_gang->owner().scans().maybeScan(m_gang->owner(), *this, nowMs));
        }
        // The reaction goal, else the goal stack; then the actions.
        if (!updateReactionGoal()) {
            processGoals();
        }
        runActions();
    }
    m_attackWarnings = 0;
    m_lastUpdateMs = nowMs;
    ++m_updates;
    m_retired.clear();
}

void Brain::think(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    if (!m_dead) {
        ++m_thinks;
    }
    // The cop's, the gang soldier's and the Warrior's think handlers keep the tackle meter.
    if (!m_dead && (m_type == BrainType::Cop || m_type == BrainType::Gang || m_type == BrainType::Warrior)) {
        thinkTackle(*this);
    }
    // A Warrior's step 7: the swap prompt beside a player. The original's Warrior still thinks while knocked out and
    // clears it then; Coney turns a script-downed brain off (BrDead), so such a brain clears it here. Coney's choice:
    // another dead brain (a script's BrDead) keeps whatever the script set.
    if (m_type == BrainType::Warrior) {
        if (m_dead) {
            if (m_human->script().knockedOut) {
                m_human->script().talkable = false;
            }
        } else {
            thinkSwapPrompt(*this);
        }
    }
}

void Brain::setDead(bool dead) {
    clearActions();
    m_dead = dead;
    // A player's brain gives up the pad while dead.
    if (m_type == BrainType::Player && m_padControl) {
        m_padControl(!dead);
    }
}

Brain* Brain::knockOut() {
    Brain* foe = m_target;
    if (m_reaction != nullptr) {
        m_reaction->end(*this);
        m_reaction.reset();
    }
    clearGoals();
    m_goalBase = -1;
    clearActions();
    {
        // Human_DropTarget from Brain_OnKnockedOut (docs/research/gang-fights.md#set-target-causes).
        const TargetCauseScope cause(*this, TargetCause::Reaction);
        setTarget(nullptr);
    }
    m_enemies.clear();
    m_knockedOut = true;
    return foe;
}

void Brain::setType(BrainType type) {
    if (type == m_type) {
        return;
    }
    m_type = type;
    m_human->fighter().setWarriorBrain(type == BrainType::Warrior);
    // An AI brain has no pad to hand back while dead (the scripts flush the goals of a new player themselves).
    if (type != BrainType::Player) {
        m_padControl = {};
    }
}

void Brain::setCharacterClass(int characterClass) {
    m_characterClass = characterClass;
    m_human->setCharacterClass(characterClass);
}

bool Brain::onEvent(const BrainEvent& event) {
    // A dead brain's handler C is a stub.
    if (m_dead) {
        return false;
    }
    return answerEvent(*this, event);
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
    // Off the stack before its End: an End can reach Lua (a flag's message 8), whose handler may push the next goal
    // (level99's `P1.ReachCenter` pushes GoalAddressPerson), which must stay above, not be popped in its place.
    std::unique_ptr<Goal> ended = std::move(m_goals.back());
    m_goals.pop_back();
    ended->end(*this);
    m_retired.push_back(std::move(ended));
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

Goal* Brain::findGoalAboveBase(GoalType type) {
    for (auto k = static_cast<int>(m_goals.size()) - 1; k > m_goalBase; --k) {
        if (m_goals.at(static_cast<std::size_t>(k))->type() == type) {
            return m_goals.at(static_cast<std::size_t>(k)).get();
        }
    }
    return nullptr;
}

Brain* Brain::nearestPlayer() const {
    Brain* best = nullptr;
    float bestDistance = 0.0F;
    if (m_peers == nullptr) {
        return nullptr;
    }
    for (const std::unique_ptr<Brain>& peer : *m_peers) {
        if (peer.get() == this || peer->type() != BrainType::Player) {
            continue;
        }
        const float d = distanceTo(*peer);
        if (best == nullptr || d < bestDistance) {
            best = peer.get();
            bestDistance = d;
        }
    }
    return best;
}

void Brain::markGoalBase() {
    if (!m_goals.empty()) {
        m_goalBase = static_cast<int>(m_goals.size()) - 1;
    }
}

void Brain::popToGoalBase() {
    while (static_cast<int>(m_goals.size()) - 1 > m_goalBase) {
        popGoal();
    }
    m_goalBase = -1;
}

bool Brain::pushTacticGoal(std::unique_ptr<Goal> goal) {
    popToGoalBase();
    markGoalBase();
    return pushGoal(std::move(goal));
}

Goal* Brain::findGoal(GoalType type) {
    for (auto it = m_goals.rbegin(); it != m_goals.rend(); ++it) {
        if ((*it)->type() == type) {
            return it->get();
        }
    }
    return nullptr;
}

bool Brain::hasFightGoal() { return findGoal(GoalType::Melee) != nullptr || findGoal(GoalType::LeftTurf) != nullptr; }

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

void Brain::finishAction(Action& action) {
    // A finished action is still aborted before it is freed, so its Abort undoes what its Start did however it ends;
    // its answer is ignored.
    static_cast<void>(action.abort(*this));
    popAction();
}

bool Brain::clearActions() {
    while (m_actionCount > 0) {
        if (!m_actions[m_actionFront]->abort(*this)) {
            return false;
        }
        popAction();
    }
    return true;
}

Action* Brain::frontAction() { return m_actionCount == 0 ? nullptr : m_actions[m_actionFront].get(); }

const Action* Brain::frontAction() const { return m_actionCount == 0 ? nullptr : m_actions[m_actionFront].get(); }

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
            // A goal that changed the stack under itself (a new fight pops the old one's goals) is not popped again.
            if (topGoal() == top) {
                popGoal();
            }
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
            finishAction(*action);
            return;
        }
    }
    if (action->update(*this) == ActionStatus::Done) {
        finishAction(*action);
    }
}

void Brain::startFight(Brain& target) {
    clearActions();
    static_cast<void>(fight(target, kNoFightLimit));
}

bool Brain::fight(Brain& target, int durationMs) {
    if (m_threatResponse == 0 || &target == this) {
        return false;
    }
    addEnemy(target);
    setTarget(&target);
    pushFightGoals(durationMs);
    return true;
}

void Brain::pushFightGoals(int durationMs) {
    // Not under a tactic, which fights for the gang, nor for a human down or out of health.
    if (m_gang != nullptr && m_gang->tactic() != nullptr) {
        return;
    }
    if (m_human->fighter().health().depleted() || m_human->state() == human::TargetState::Grounded) {
        return;
    }
    // The last fight's Melee and FindEnemy go, with everything above them.
    const auto oldest = std::ranges::find_if(m_goals, [](const std::unique_ptr<Goal>& goal) {
        return goal->type() == GoalType::Melee || goal->type() == GoalType::FindEnemy;
    });
    const auto keep = static_cast<std::size_t>(oldest - m_goals.begin());
    // Coney's searching FindEnemy (GoalMelee's stand-in) stays a searching one.
    const bool searches = std::any_of(oldest, m_goals.end(), [](const std::unique_ptr<Goal>& goal) {
        return goal->type() == GoalType::FindEnemy && static_cast<const FindEnemyGoal&>(*goal).searches();
    });
    while (m_goals.size() > keep) {
        popGoal();
    }
    // Goal_Melee's two, then the fight goal on top.
    pushGoal(std::make_unique<FindEnemyGoal>(durationMs, m_nowMs, searches));
    pushGoal(std::make_unique<MeleeGoal>(durationMs));
    pushGoal(std::make_unique<FightGoal>(durationMs));
}

bool Brain::listEnemy(Brain& enemy) {
    if (&enemy == this || m_enemies.size() >= kMaxEnemies || std::ranges::find(m_enemies, &enemy) != m_enemies.end()) {
        return false;
    }
    m_enemies.push_back(&enemy);
    return true;
}

void Brain::addEnemy(Brain& enemy) {
    if (!listEnemy(enemy)) {
        return;
    }
    // The enemy takes this human as its enemy too, unless both are players.
    if (m_type != BrainType::Player || enemy.m_type != BrainType::Player) {
        enemy.listEnemy(*this);
    }
    // A tactic that runs its members hears of the new enemy.
    if (m_gang != nullptr && m_gang->tactic() != nullptr && !m_gang->tactic()->alertsGang()) {
        // A target the tactic sets here is its own answer, not the caller's cause (a hit back or a help call that
        // listed the enemy): docs/research/gang-fights.md#set-target-causes.
        const TargetCauseScope cause(*this, TargetCause::Other);
        m_gang->tactic()->event(*m_gang, *this, BrainEvent{.id = kEventEnemyAdded, .other = &enemy});
    }
}

void Brain::setTarget(Brain* target, bool force) {
    if (m_target == target) {
        if (m_target != nullptr) {
            m_target->claimSlot(*this, force);
        }
        return;
    }
    if (m_target != nullptr) {
        m_target->releaseSlot(*this);
    }
    ++m_targetChanges.at(static_cast<std::size_t>(m_targetCause));
    m_target = target;
    if (m_target != nullptr) {
        m_target->claimSlot(*this, force);
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

bool Brain::claimSlot(Brain& attacker, bool force) {
    if (std::ranges::find(m_slots, &attacker) != m_slots.end()) {
        raiseSpacing(attacker);
        return true;
    }
    if (m_slots.size() < m_slotCount) {
        m_slots.push_back(&attacker);
        raiseSpacing(attacker);
        return true;
    }
    // Full: a closer attacker, or one forced in, takes the farthest one's slot, and the farthest loses his target.
    const auto farthest = std::ranges::max_element(m_slots, {}, [this](const Brain* b) { return distanceTo(*b); });
    if (farthest == m_slots.end() || (!force && distanceTo(attacker) >= distanceTo(**farthest))) {
        return false;
    }
    m_fight.places.release(*farthest);
    if ((*farthest)->m_target == this) {
        (*farthest)->m_target = nullptr;
        ++(*farthest)->m_targetChanges.at(static_cast<std::size_t>(TargetCause::Displaced));
    }
    *farthest = &attacker;
    raiseSpacing(attacker);
    return true;
}

void Brain::releaseSlot(const Brain& attacker) {
    std::erase(m_slots, &attacker);
    m_fight.places.release(&attacker);
    // The spacing returns to 1 once nobody targets this human (`Brain_SetTarget`).
    if (m_slots.empty()) {
        m_fight.spacing.reset();
    }
}

void Brain::raiseSpacing(const Brain& attacker) {
    const GangFightValues values = attacker.gangFight();
    m_fight.spacing.raise(values.standingSpacing, values.downSpacing, m_type == BrainType::Warrior);
}

GangFightValues Brain::gangFight() const {
    if (m_gang == nullptr || m_gang->kind() < 0 || m_gang->kind() >= static_cast<int>(kGangKinds)) {
        return {};
    }
    return m_settings.gangFight[static_cast<std::size_t>(m_gang->kind())];
}

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
    m_human->record().move =
        human::BrainMove{.heading = heading, .speed = speed, .turnBoost = m_turnBoost, .aim = m_moveAim};
}

void Brain::setMoveHeading(float heading, float speed) {
    m_human->record().move = human::BrainMove{
        .heading = human::wrapAngle(heading), .speed = speed, .turnBoost = m_turnBoost, .aim = m_moveAim};
}

void Brain::setTurnBoost(int boost) {
    m_turnBoost = boost;
    if (std::optional<human::BrainMove>& move = m_human->record().move) {
        move->turnBoost = boost;
    }
}

void Brain::setMoveAim(anim::Vec3 point, float radius) {
    m_moveAim = point;
    m_moveAimRadius = radius;
    // A move already written this update aims there too, whichever of the two its writer set first.
    if (std::optional<human::BrainMove>& move = m_human->record().move) {
        move->aim = point;
    }
}

void Brain::stopMove() { m_human->record().move.reset(); }

void Brain::requestClimb(anim::Vec3 direction) { m_human->record().climbToward = direction; }

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

bool Brain::canSee(const Brain& other, float range) const {
    return canSeeHuman(m_collision, *m_human, other.human(), range);
}

bool Brain::hasLineOfSight(const Brain& other) const {
    return lineOfSight(m_collision, m_human->position(), other.human().position()).clear;
}

bool Brain::fightable(const Brain& other) { return !other.human().fighter().health().depleted(); }

bool deliverEvent(Brain& brain, const BrainEvent& event) {
    if (brain.services() != nullptr && brain.services()->humanEvent(brain, event)) {
        return true;
    }
    if (brain.gang() != nullptr && brain.gang()->onEvent(brain, event)) {
        return true;
    }
    return brain.onEvent(event);
}

void Brain::forget(const Brain& other) {
    if (m_target == &other) {
        m_target = nullptr;
        ++m_targetChanges.at(static_cast<std::size_t>(TargetCause::Other)); // the target left the scene
    }
    std::erase(m_enemies, &other);
    std::erase(m_slots, &other);
    m_sectors.forget(other);
    // A detour round him ends.
    if (m_steering.avoiding == &other) {
        setAvoiding(*this, nullptr);
    }
}

Sectors& Brain::sectors(std::uint64_t maxAgeMs) {
    // Rebuilt from every brain of the scene when due.
    if (m_peers != nullptr && m_sectors.due(m_nowMs, maxAgeMs)) {
        std::vector<const Brain*> others;
        others.reserve(m_peers->size());
        for (const std::unique_ptr<Brain>& peer : *m_peers) {
            others.push_back(peer.get());
        }
        m_sectors.rebuild(*this, others, m_nowMs);
    }
    return m_sectors;
}

void releaseToAi(Brain& brain, BrainType leftAs) {
    // Its class's type first: an AI brain has no pad to hand back (Brain::setType()), so no later BrDead gives the pad
    // to the human again.
    brain.setType(leftAs);
    brain.clearGoals();
    static_cast<void>(brain.clearActions());
    // The flags Human_ReleasePlayer leaves for that type: rage gone, revivable again for a Warrior.
    human::Human& human = brain.human();
    const std::uint64_t released = releasedFlags(human.flags(), leftAs);
    human.setFlag(~released, false);
    human.setFlag(released, true);
}

int reactionKind(const Brain& brain) {
    constexpr int kBossRole = 13;
    if (brain.type() == BrainType::Player) {
        return static_cast<int>(BrainType::Warrior);
    }
    if (brain.type() == BrainType::Gang) {
        const Gang* gang = brain.gang();
        if (gang != nullptr && gang->owner().roleOf(brain) == kBossRole) {
            return 0;
        }
    }
    return static_cast<int>(brain.type());
}

int helpLevel(const Brain& brain) {
    constexpr int kSoloRole = 6;
    constexpr int kConfrontTacticType = 0x23;
    switch (brain.type()) {
    case BrainType::Cop:
    case BrainType::Warrior:
        return 1;
    case BrainType::Gang:
    case BrainType::Civilian: {
        // The class role and the gang's tactic each bring it down to one.
        const Gang* gang = brain.gang();
        if (gang == nullptr) {
            return 2;
        }
        const Tactic* tactic = gang->tactic();
        const bool confront = tactic != nullptr && tactic->type() == kConfrontTacticType;
        return gang->owner().roleOf(brain) == kSoloRole || confront ? 1 : 2;
    }
    default:
        return 2;
    }
}

} // namespace coney::ai
