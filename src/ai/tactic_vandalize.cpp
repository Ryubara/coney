// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_vandalize.h"

#include <memory>

#include "ai/brain.h"
#include "ai/destroy_goal.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/item_services.h"
#include "ai/melee_goal.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// The member events the tactic answers beyond the base's: 19 and 22 give the goals again, 20 (a fight seen) is taken.
constexpr int kEventRegoal = 19;
constexpr int kEventGoalsAgain = 22;

// @coney-only helper: GiveGoals' member test, an AI human who is up (not down or dead: Human_IsDownOrDead)
bool upAndAi(const Brain& brain) {
    return brain.type() != BrainType::Player && Brain::fightable(brain) &&
           brain.human().state() != human::TargetState::Grounded;
}

} // namespace

void VandalizeTactic::giveGoals(Gang& gang) {
    ItemServices* items = services().items;
    if (items == nullptr) {
        return;
    }
    const script::TacticCall& order = call();
    for (Brain* member : gang.members()) {
        if (upAndAi(*member)) {
            // Coney stand-in: the vandal brain byte +0x28d is not set (not identified).
            static_cast<void>(member->pushTacticGoal(
                std::make_unique<DestroyGoal>(static_cast<int>(order.zone), order.delayMs, order.range, *items)));
        }
    }
}

void VandalizeTactic::start(Gang& gang) {
    // The first check comes 3 s after the tactic is made, which is its first update.
    m_nextCheckMs = gang.owner().nowMs() + kVandalizeCheckMs;
    giveGoals(gang);
}

int VandalizeTactic::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    if (now <= m_nextCheckMs) {
        return 0;
    }
    m_nextCheckMs = now + kVandalizeCheckMs;
    // Coney stand-in: the cars of TacticVandalizeCars are not built (issue #716), so the zone decides.
    const ItemServices* items = services().items;
    if (items != nullptr && items->zoneHasObjects(static_cast<int>(call().zone))) {
        return 0;
    }
    return kTacTimeUp;
}

void VandalizeTactic::onDamage(Brain& member, Brain& attacker) {
    if (Gangs::friends(member.gang(), attacker.gang())) {
        return;
    }
    if (member.hasFightGoal()) {
        member.addEnemy(attacker);
        const TargetCauseScope cause(member, TargetCause::HitBack);
        member.setTarget(&attacker, true);
        return;
    }
    // Down to his DestroyGoal, then the fight on top of it.
    if (const Goal* destroy = member.findGoal(GoalType::Destroy); destroy != nullptr) {
        while (member.topGoal() != destroy) {
            member.popGoal();
        }
    }
    member.addEnemy(attacker);
    static_cast<void>(member.pushGoal(std::make_unique<MeleeGoal>(kVandalizeMeleeMs)));
    static_cast<void>(member.pushGoal(std::make_unique<FightGoal>(kVandalizeFightMs)));
}

bool VandalizeTactic::event(Gang& gang, Brain& member, const BrainEvent& event) {
    switch (event.id) {
    case kEventDamaged:
        // Coney stand-in: the event's flag +0x11 the original also asks for is not modelled.
        if (event.other != nullptr) {
            onDamage(member, *event.other);
        }
        return StoryTactic::event(gang, member, event);
    case kEventRegoal:
    case kEventGoalsAgain:
        giveGoals(gang);
        return false;
    case kEventViolence:
        return true;
    default:
        // 10 → 4, 11 → 3, 16 → 6; the others are not answered. Coney stand-ins: event 11's spotter is not noted
        // (Brain_NoteSpotter), and Coney sends no event 23 (an object broken), so its cheer
        // (VandalizeTactic_OnObjectBroken, with DestroyGoal_IsWaiting) and code 16 for a broken object of the zone
        // are not built (issue #1571).
        return StoryTactic::event(gang, member, event);
    }
}

} // namespace coney::ai
