// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brain_events.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/reaction_goals.h"
#include "ai/route_planner.h"
#include "ai/turn_action.h"
#include "combat/ai_counter.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "human/fighter.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// The most humans one help call reaches (`Humans_FindAhead`'s list).
constexpr int kMostHearers = 60;
// The gang kind whose members leave violence alone (`0x17`).
constexpr int kIgnoresViolenceKind = 0x17;
// HelpRespond: a target within this share of the far range is watched, not walked to.
constexpr float kNearShare = 1.1F;
// HelpRespond: the look at a near target, ms.
constexpr std::uint64_t kLookMs = 3000;
// HelpRespond: beyond this distance, or with no enemies listed, he runs, metres.
constexpr float kRunBeyond = 10.0F;
constexpr int kWalkGait = 2;
constexpr int kRunGait = 4;
// HelpRespond: how often, in updates, it checks for enemies listed.
constexpr int kEnemyCheckUpdates = 30;

// Whether `brain`'s human is busy (`Human_IsBusy`: his record bits and his state).
bool busy(const Brain& brain) { return humanBusy(brain.human()); }

// Whether `other` is a friend of `brain` (their gangs, `Gang_AreFriends`).
bool friendly(const Brain& brain, const Brain& other) { return Gangs::friends(brain.gang(), other.gang()); }

// Whether `other` is a threat to `brain` (`Brain_IsThreatTo`). **Coney stand-in**: on his enemy list, or of a gang
// his gang has as an enemy.
bool threatTo(const Brain& brain, const Brain& other) {
    return std::ranges::find(brain.enemies(), &other) != brain.enemies().end() ||
           (brain.gang() != nullptr && other.gang() != nullptr && Gangs::enemies(brain.gang(), other.gang()));
}

// A hit's answer by a soldier or a Warrior: the gangs made enemies and, unless he is busy, a fight with the hitter (or
// the hitter as the target when already fighting). Returns whether the hitter is not a friend (the hit taken).
bool fightHitter(Brain& brain, Brain& hitter) {
    if (friendly(brain, hitter) || &hitter == &brain) {
        return false;
    }
    makeGangsEnemies(brain, hitter);
    if (busy(brain)) {
        return true;
    }
    if (brain.findGoal(GoalType::Fight) != nullptr) {
        brain.addEnemy(hitter);
        brain.setTarget(&hitter);
    } else {
        static_cast<void>(brain.fight(hitter, kNoFightLimit));
    }
    return true;
}

// The gang soldier's handler (`GangBrain_OnEvent`): a hit by a stranger, violence seen; nothing for the other ids
// (the shared handler takes them). **Coney stand-ins**: the top goals' own hit handlers (GrabTarget, BumLogic, the
// boss goals, Riot) are not reached here (a boss's tactic takes its hits first); a hitter throwing at him gets no
// help call first; the hitter's head does not turn to a busy soldier.
bool gangEvent(Brain& brain, const BrainEvent& event) {
    if (event.id == kEventDamaged && event.other != nullptr && !friendly(brain, *event.other)) {
        const bool wasBusy = busy(brain);
        static_cast<void>(fightHitter(brain, *event.other));
        // Busy, hit by a player: a call to everyone within 30 m.
        if (wasBusy && event.other->type() == BrainType::Player) {
            broadcastHelpCall(brain, kHelpCallBusy, event.other);
        }
        return true;
    }
    if (event.id == kEventViolence) {
        if (brain.gang() == nullptr || brain.gang()->kind() != kIgnoresViolenceKind) {
            onViolenceSeen(brain, event);
        }
        return true;
    }
    return false;
}

// The Warrior's handler (`WarriorBrain_OnEvent`): a hit by a stranger, a new enemy noted (taken), violence seen; the
// hit and violence go on to the shared handler. **Coney stand-ins**: the prompt (0) and the chief's hit
// (`WarriorBrain_OnHitByChief`) are not built; danger near (`0x18`) has no sender.
// Returns whether the id was taken (the shared handler is then skipped).
bool warriorEvent(Brain& brain, const BrainEvent& event) {
    constexpr int kEventIgnored = 0x0c;
    switch (event.id) {
    case kEventDamaged:
        if (event.other != nullptr) {
            static_cast<void>(fightHitter(brain, *event.other));
        }
        return false;
    case kEventEnemyAdded:
    case kEventIgnored:
        return true;
    case kEventViolence:
        onViolenceSeen(brain, event);
        return false;
    default:
        return false;
    }
}

// The shared handler (`Brain_DefaultOnEvent`): a hit sends a help call naming the hitter; the reset clears the
// target, the actions, the goals and the enemies; a new enemy sends a help call flagged new; an attack warning is
// counted, makes the gangs enemies, sends a help call and, under a fight goal, may be countered. **Coney stand-ins**:
// the attack warning's sight test runs where it is announced (Brain::update()); the StationaryShooter and BigFighter
// answers, the pattern block and the lull's taunts (`0x15`) are not built.
bool sharedEvent(Brain& brain, const BrainEvent& event) {
    switch (event.id) {
    case kEventDamaged:
        if (event.other == nullptr) {
            return false;
        }
        broadcastHelpCall(brain, kHelpCallShared, event.other, event.value);
        return true;
    case kEventReset:
        if (brain.type() == BrainType::Cop) {
            return false;
        }
        brain.setTarget(nullptr);
        brain.clearActions();
        brain.clearGoals();
        brain.clearEnemies();
        return true;
    case kEventEnemyAdded:
        if (event.other == nullptr) {
            return false;
        }
        broadcastHelpCall(brain, kHelpCallShared, event.other, 0, true);
        return true;
    case kEventAttackWarning: {
        brain.countAttackWarning();
        Brain* attacker = event.other;
        if (attacker == nullptr || friendly(brain, *attacker)) {
            return true;
        }
        makeGangsEnemies(brain, *attacker);
        broadcastHelpCall(brain, kHelpCallShared, attacker);
        // Under a fight goal, a grab or tackle on his way in may be countered (command 3).
        const Goal* top = brain.topGoal();
        const Brain* target = brain.target();
        if (top != nullptr && top->type() == GoalType::Fight && target != nullptr &&
            combat::aiCounterFor(static_cast<int>(target->human().animator().animId())) != combat::anim_id::kNone &&
            static_cast<float>(brain.rand100()) < brain.counterChance()) {
            brain.clearActions();
            if (brain.actionCount() == 0) {
                brain.press(combat::command::kR1Pressed);
            }
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace

bool answerEvent(Brain& brain, const BrainEvent& event) {
    switch (brain.type()) {
    case BrainType::Gang:
        if (gangEvent(brain, event)) {
            return true;
        }
        break;
    case BrainType::Warrior:
        if (warriorEvent(brain, event)) {
            return true;
        }
        break;
    case BrainType::Player:
        // The player drops violence.
        if (event.id == kEventViolence) {
            return false;
        }
        break;
    default:
        break;
    }
    return sharedEvent(brain, event);
}

void broadcastHelpCall(Brain& caller, float range, Brain* aggressor, int value, bool newEnemy) {
    const std::vector<std::unique_ptr<Brain>>* peers = caller.peers();
    if (peers == nullptr) {
        return;
    }
    // Who is in reach first, then the calls: a hearer's answer may not change the list under the loop.
    std::vector<Brain*> hearers;
    for (const std::unique_ptr<Brain>& peer : *peers) {
        if (peer.get() == &caller) {
            continue;
        }
        const float distance = caller.distanceTo(*peer);
        if (distance <= range && distance <= peer->senses().helpHearRange) {
            hearers.push_back(peer.get());
            if (static_cast<int>(hearers.size()) == kMostHearers) {
                break;
            }
        }
    }
    for (Brain* hearer : hearers) {
        static_cast<void>(deliverEvent(
            *hearer,
            BrainEvent{
                .id = kEventViolence, .other = aggressor, .value = value, .victim = &caller, .newEnemy = newEnemy}));
    }
}

Brain* pickSideInFight(Brain& hearer, const BrainEvent& event) {
    Brain* victim = event.victim;
    Brain* aggressor = event.other;
    // No aggressor named: the victim's target.
    if (aggressor == nullptr && victim != nullptr) {
        aggressor = victim->target();
    }
    if (aggressor == nullptr || victim == nullptr || aggressor == &hearer) {
        return nullptr;
    }
    const bool victimFriend = victim == &hearer || friendly(hearer, *victim);
    const bool aggressorFriend = friendly(hearer, *aggressor);
    bool aggressorThreat = threatTo(hearer, *aggressor);
    // A stranger hitting a friend: the gangs become enemies (not for a cop) and he is a threat.
    if (victimFriend && !aggressorFriend && !aggressorThreat) {
        if (hearer.type() != BrainType::Cop) {
            makeGangsEnemies(hearer, *aggressor);
        }
        aggressorThreat = true;
    }
    if (aggressorThreat) {
        return aggressor;
    }
    if (aggressorFriend && !victimFriend) {
        return victim;
    }
    return nullptr;
}

void onViolenceSeen(Brain& brain, const BrainEvent& event) {
    // Only a man free to take part: a threat response, no target, nothing held, free actions, not closing in.
    if (brain.threatResponse() == 0 || brain.target() != nullptr || brain.human().animator().flags() != 0 ||
        brain.actionCount() > 0) {
        return;
    }
    if (const Goal* top = brain.topGoal(); top != nullptr && top->type() == GoalType::EngageEnemy) {
        return;
    }
    Brain* side = pickSideInFight(brain, event);
    if (side == nullptr || side == &brain || !Brain::fightable(*side)) {
        return;
    }
    if (brain.findGoal(GoalType::Fight) != nullptr) {
        brain.setTarget(side);
    } else {
        static_cast<void>(brain.fight(*side, kNoFightLimit));
    }
    if (brain.findGoalAboveBase(GoalType::HelpRespond) == nullptr) {
        sendHelper(brain, *side);
    }
}

void makeGangsEnemies(const Brain& brain, const Brain& other) {
    Gang* mine = brain.gang();
    const Gang* theirs = other.gang();
    if (mine == nullptr || theirs == nullptr || mine == theirs || Gangs::friends(mine, theirs) ||
        Gangs::enemies(mine, theirs)) {
        return;
    }
    mine->owner().makeEnemies(mine->id(), theirs->id());
}

void sendHelper(Brain& brain, Brain& side) {
    static_cast<void>(brain.pushGoal(std::make_unique<HelpRespondGoal>(side)));
}

void HelpRespondGoal::start(Brain& brain) {
    // He may have left the scene since the call.
    const std::vector<std::unique_ptr<Brain>>* peers = brain.peers();
    if (peers == nullptr ||
        std::ranges::none_of(*peers, [this](const std::unique_ptr<Brain>& peer) { return peer.get() == m_target; })) {
        m_target = nullptr;
        return;
    }
    if (!Brain::fightable(*m_target)) {
        return;
    }
    const anim::Vec3 there = m_target->human().position();
    const float distance = brain.distanceTo(*m_target);
    const float near = kNearShare * brain.meleeFar();
    // Close by with a walkable line: turn to him and watch him for 3 s.
    const RoutePlanner* planner = brain.planner();
    const bool walkable = planner == nullptr || planner->lineClear(brain.human().position(), there);
    if (distance <= near && walkable) {
        brain.queueAction(TurnAction::toPoint(there));
        m_lookUntilMs = brain.nowMs() + kLookMs;
        return;
    }
    // Otherwise go over, running when far or with no enemies listed yet.
    const bool run = distance > kRunBeyond || brain.enemies().empty();
    brain.queueAction(std::make_unique<MoveAction>(
        MoveRequest{.point = there, .radius = near, .gait = run ? kRunGait : kWalkGait, .delayMs = 0}));
}

GoalStatus HelpRespondGoal::process(Brain& brain) {
    // Every 30 updates: a listed enemy means a fight takes over.
    if (++m_updates % kEnemyCheckUpdates == 0 && !brain.enemies().empty()) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0 || brain.nowMs() < m_lookUntilMs) {
        return GoalStatus::Stop;
    }
    return GoalStatus::Done;
}

void HelpRespondGoal::end(Brain& brain) { brain.clearActions(); }

} // namespace coney::ai
