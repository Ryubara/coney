// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_boss.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

#include "ai/boss_goals.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/riot_goals.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// The tactic's code for a finished stage (`TacFinished`).
constexpr int kTacFinished = 1;
// The health caps: at or below the cap he is put back one point above it until he is tired, percent.
constexpr float kFirstCap = 66.0F;
constexpr float kSecondCap = 33.0F;
constexpr float kCapStep = 1.0F;
// The minions' wait between throws, seconds. **Coney choice**: the tactic's StationaryThrower delay is not on the page.
constexpr int kMinionThrowDelay = 3;

// The table entry of boss `which` (0 Diego, 1 Vargas) for every stage.
BossTables tablesOf(const script::TacticCall& call, std::size_t which, int stage) {
    const script::BossScenarioCall& boss = call.boss;
    BossTables tables;
    tables.fatigue = boss.fatigue.at(which);
    tables.damage = boss.damage.at(which);
    tables.prone = boss.prone.at(which);
    tables.cycles = boss.cycles.at(which).at(static_cast<std::size_t>(std::clamp(stage, 1, 3) - 1));
    return tables;
}

// Whether `member` holds a goal this tactic gives (a tired boss keeps his BigBrawler under the tired goal).
bool hasBossGoal(Brain& member) {
    return member.findGoal(GoalType::BigBrawler) != nullptr || member.findGoal(GoalType::BigThrower) != nullptr ||
           member.findGoal(GoalType::StationaryThrower) != nullptr;
}

// Whether `member` can be given a goal: an AI human neither out of health nor cuffed.
bool canTakeGoal(const Brain& member) {
    return member.type() != BrainType::Player && Brain::fightable(member) && member.human().alive();
}

} // namespace

BossDiegoVargasTactic::BossDiegoVargasTactic(const script::TacticCall& call, const TacticServices& services)
    : StoryTactic(call, services), m_stage(std::clamp(call.boss.stage, 1, 3)) {}

void BossDiegoVargasTactic::start(Gang& gang) {
    for (Brain* member : gang.members()) {
        if (canTakeGoal(*member)) {
            assignGoal(*member);
        }
    }
}

void BossDiegoVargasTactic::assignGoal(Brain& member) {
    const script::TacticCall& c = call();
    ScriptServices* scripts = services().scripts;
    const FlagServices* flags = services().flags;
    const int character = member.characterClass();
    constexpr std::size_t kDiego = 0;
    constexpr std::size_t kVargas = 1;
    if (character == kDiegoClass) {
        BigBrawlerOrder order{.tables = tablesOf(c, kDiego, m_stage), .stage = m_stage, .flag = 0, .objects = {}};
        auto goal = std::make_unique<BigBrawlerGoal>(order, scripts, flags);
        goal->setStage(m_stage);
        member.pushTacticGoal(std::move(goal));
        return;
    }
    if (character == kVargasClass && m_stage == 2) {
        const BossTables tables = tablesOf(c, kVargas, m_stage);
        member.pushTacticGoal(std::make_unique<BigThrowerGoal>(BigThrowerOrder{.flag = c.flags.at(0),
                                                                               .cycles = tables.cycles,
                                                                               .fatigue = tables.fatigue.at(1),
                                                                               .damage = tables.damage.at(1),
                                                                               .objects = c.boss.vargasObjects},
                                                               scripts, flags));
        return;
    }
    if (character == kVargasClass && m_stage == 3) {
        BigBrawlerOrder order{.tables = tablesOf(c, kVargas, m_stage),
                              .stage = m_stage,
                              .flag = c.flags.at(1),
                              .objects = c.boss.vargasObjects};
        auto goal = std::make_unique<BigBrawlerGoal>(order, scripts, flags);
        goal->setStage(m_stage);
        member.pushTacticGoal(std::move(goal));
        return;
    }
    if (scripts != nullptr) {
        member.pushTacticGoal(
            std::make_unique<StationaryThrowerGoal>(kMinionThrowDelay, c.boss.minionObjects, *scripts));
    }
}

int BossDiegoVargasTactic::update(Gang& gang) {
    int code = 0;
    for (Brain* member : gang.members()) {
        // A member left without the tactic's goal (respawned, or his goal ended) gets his again.
        if (canTakeGoal(*member) && !hasBossGoal(*member)) {
            assignGoal(*member);
        }
        const int character = member->characterClass();
        if ((character == kDiegoClass || character == kVargasClass) && code == 0) {
            code = checkHealth(*member);
        }
    }
    if (code != 0) {
        return code;
    }
    if (m_stage == 3 && !anyBossStanding(gang) && !m_finished) {
        m_finished = true;
        return kTacFinished;
    }
    return 0;
}

int BossDiegoVargasTactic::checkHealth(Brain& boss) {
    const bool diego = boss.characterClass() == kDiegoClass;
    // The cap for this boss and stage: Diego's in stages 1 and 2, Vargas's two breaks in stage 3.
    float cap = 0.0F;
    if (diego && m_stage < 3) {
        cap = m_stage == 1 ? kFirstCap : kSecondCap;
    } else if (!diego && m_stage == 3 && m_breaks < 2) {
        cap = m_breaks == 0 ? kFirstCap : kSecondCap;
    }
    TiredGoal* tired = tiredGoalOf(boss);
    // A break running: 18 while its clip plays (once), then its end.
    if (tired != nullptr && tired->breaking()) {
        if (tired->breakClipPlaying()) {
            return 0;
        }
        if (!tired->breakDone(boss)) {
            return 0;
        }
        if (diego) {
            if (m_finished) {
                return 0;
            }
            m_finished = true;
            return kTacFinished;
        }
        tired->endBreak(boss);
        ++m_breaks;
        return 0;
    }
    if (cap <= 0.0F || !Brain::fightable(boss) || boss.human().healthPercent() > cap) {
        return 0;
    }
    // At the cap: held above it until he is tired; tired, the break starts.
    if (tired == nullptr) {
        boss.human().setHealthPercent(cap + kCapStep);
        return 0;
    }
    tired->startBreak(boss, cap);
    return kTacAnimStart;
}

bool BossDiegoVargasTactic::anyBossStanding(const Gang& gang) const {
    if (m_stage != 3) {
        return true;
    }
    return std::ranges::any_of(gang.members(), [](const Brain* member) {
        const int character = member->characterClass();
        return (character == kDiegoClass || character == kVargasClass) && Brain::fightable(*member);
    });
}

bool BossDiegoVargasTactic::event(Gang& /*gang*/, Brain& member, const BrainEvent& event) {
    constexpr int kEventConsumed = 20;
    auto* brawler = static_cast<BigBrawlerGoal*>(member.findGoal(GoalType::BigBrawler));
    switch (event.id) {
    case kEventDamaged:
        if (brawler != nullptr) {
            brawler->onHit(member);
        }
        return true;
    case kEventAttackWarning:
        if (brawler != nullptr) {
            brawler->onAttackWarning(member, event.other);
        }
        return true;
    case kEventConsumed:
        return true;
    default:
        return false;
    }
}

TiredGoal* tiredGoalOf(Brain& brain) {
    Goal* top = brain.topGoal();
    return top != nullptr && top->type() == GoalType::Tired ? static_cast<TiredGoal*>(top) : nullptr;
}

} // namespace coney::ai
