// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/face_mood.h"

#include <optional>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/tactic.h"

namespace coney::ai {

namespace {

// The goal type id of the chase goal `Brain_HasChaseGoal` looks for besides LeftTurf; Coney builds no goal of it.
constexpr auto kChaseGoal = static_cast<GoalType>(0x0c);

// The mood a top goal's type gives, or none: the type leaves the mood as the fight and chase tests set it.
// @orig 0x0028bbf0 Brain_UpdateAlertness (unknown)
std::optional<human::FaceMood> moodOfGoal(int type) {
    switch (type) {
    case 0x4f: // BumLogic
    case 0x50: // Peddler
    case 0x54: // Riot
    case 0x62:
    case 0x63:
    case 0x71:
    case 0x80: // Dealer
        return human::FaceMood::Excited;
    case 0x09:
    case 0x4a: // RunCarrot
    case 0x52:
    case 0x72: // Hostile
    case 0x7c:
    case 0x8b:
    case 0x98: // DevilRun
        return human::FaceMood::Angry;
    case 0x02: // MoveToExitFlag
    case 0x07:
    case 0x11:
    case 0x20:
    case 0x49: // LeadChase
    case 0x6b: // PedestrianReaction
    case 0x6d:
    case 0x6e: // CallGang
    case 0x79:
        return human::FaceMood::Scared;
    default:
        return std::nullopt;
    }
}

// The mood a gang tactic that does not alert the gang gives, by its type.
// @orig 0x0028bbf0 Brain_UpdateAlertness (unknown)
human::FaceMood moodOfTactic(int type, bool padDriven) {
    switch (type) {
    case 0x13:
    case 0x20:
    case 0x25:
        return padDriven ? human::FaceMood::Neutral : human::FaceMood::Scared;
    case 0x18:
    case 0x1b:
    case 0x24:
        return human::FaceMood::Excited;
    case 0x14:
    case 0x15:
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x22:
    case 0x23:
    case 0x26:
        return human::FaceMood::Angry;
    default:
        return human::FaceMood::Neutral;
    }
}

} // namespace

human::FaceMood faceMoodOf(const FaceMoodFacts& facts) {
    if (facts.down || facts.cuffed || facts.onFire) {
        return human::FaceMood::Scared;
    }
    if (facts.gangTactic) {
        return facts.tacticAlerts ? human::FaceMood::Angry : moodOfTactic(facts.tacticType, facts.padDriven);
    }
    const human::FaceMood fightOrNot =
        facts.fighting || facts.chasing ? human::FaceMood::Angry : human::FaceMood::Neutral;
    return moodOfGoal(facts.topGoal).value_or(fightOrNot);
}

FaceMoodFacts faceMoodFacts(Brain& brain) {
    const human::Human& human = brain.human();
    FaceMoodFacts facts{
        .down = human.downOrDead(), .cuffed = human.script().arrested, .padDriven = human.record().padDriven};
    if (const Gang* gang = brain.gang(); gang != nullptr && gang->tactic() != nullptr) {
        facts.gangTactic = true;
        facts.tacticAlerts = gang->tactic()->alertsGang();
        facts.tacticType = gang->tactic()->type();
    }
    facts.fighting = brain.hasFightGoal();
    facts.chasing = brain.findGoal(kChaseGoal) != nullptr || brain.findGoal(GoalType::LeftTurf) != nullptr;
    if (const Goal* top = brain.topGoal(); top != nullptr) {
        facts.topGoal = static_cast<int>(top->type());
    }
    return facts;
}

} // namespace coney::ai
