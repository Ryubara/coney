// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/boss_goals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>

#include "ai/attack_choice.h"
#include "ai/attack_views.h"
#include "ai/brain.h"
#include "ai/engage_goals.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/move_to_flag_goal.h"
#include "ai/move_to_human_action.h"
#include "ai/play_anim_action.h"
#include "ai/reaction_goals.h"
#include "ai/script_services.h"
#include "ai/targeting.h"
#include "ai/turn_action.h"
#include "combat/ai_counter.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The switches a boss goal's Start sets (`0x223c0`): ungrabbable, ungroundable, unstunnable, reduced reactions, keep
// weapon and auto escape.
constexpr std::uint64_t kBossFlags = human::flag::kUngrabbable | human::flag::kUngroundable |
                                     human::flag::kUnstunnable | human::flag::kReducedReact | human::flag::kKeepWeapon |
                                     human::flag::kAutoEscape;
// A boss's field of view while his goal runs (all round), and the one End gives back (1.92 rad).
constexpr float kAllRound = 2.0F * std::numbers::pi_v<float>;
constexpr float kDefaultView = 1.92F;
// The threat response End gives back.
constexpr int kThreatAgain = 2;
// The hits that tire a BigBrawler.
constexpr int kHitsToTire = 6;
// The engage: the run-in distance (2.5 m), the special's range share of far, the special's kind, the taunt chance.
constexpr float kBossRunIn = 2.5F;
constexpr float kSpecialShare = 0.55F;
constexpr int kSpecialKind = 16;
constexpr int kTauntPercent = 31;
// The busy man: wait this long after closing on him, ms.
constexpr std::uint64_t kBusyWaitMs = 1000;
// The fight: a new target every 4 s; the move into reach lasts at most 1000 ms (a boss is class 13).
constexpr std::uint64_t kRepickMs = 4000;
constexpr std::uint32_t kBossMoveMs = 1000;
constexpr float kMoveBandSlack = 0.1F;
// The score: per metre, the player's and the downed man's bonuses, the fight's range share of far.
constexpr float kPerMetre = 4.0F;
constexpr float kFightPlayer = 15.0F;
constexpr float kFightDown = 10.0F;
constexpr float kEngagePlayer = 20.0F;
constexpr float kFightRangeShare = 1.15F;
// The shove: within this of the boss, with at least this share of his health.
constexpr float kShoveRange = 1.5F;
constexpr float kShoveHealth = 0.1F;
// The flag cycle: the arrival radius, the step back, the walk's gait.
constexpr float kFlagRadius = 0.5F;
constexpr float kStepBack = 2.0F;
constexpr int kWalkGait = 2;
// The tired goal: the break's hold lasts this long before it is done, ms.
constexpr std::uint64_t kBreakHoldMs = 2000;
// The BigThrower: aim within this of the target (45 degrees), drop him after this many misses, wait after a throw past
// half the cycles, ms.
constexpr float kAimAngle = std::numbers::pi_v<float> / 4.0F;
constexpr int kAimMisses = 6;
constexpr std::uint64_t kHalfWaitMs = 2000;

// Has `brain` say `line` through `services`, when there are services.
void say(ScriptServices* services, Brain& brain, int line, bool interrupt = false) {
    if (services != nullptr) {
        services->say(brain, line, interrupt, 0.0);
    }
}

// Queues clip `animId` on `brain` through `services`, when there are services.
void play(ScriptServices* services, Brain& brain, int animId, std::int16_t delayMs = 0) {
    if (services != nullptr) {
        brain.queueAction(std::make_unique<PlayAnimAction>(*services, animId, false, delayMs));
    }
}

// Whether `brain`'s human is busy (`Human_IsBusy`: his record bits and his state).
bool busy(const Brain& brain) { return humanBusy(brain.human()); }

// Whether `brain`'s human is down (state `0xe0000`): out of health or on the ground.
bool down(const Brain& brain) {
    return brain.human().fighter().health().depleted() || brain.human().fighter().victim().grounded() ||
           brain.human().state() == human::TargetState::Grounded;
}

// **Coney stand-in** for the enemy scan (ai-core's): every fightable member of a gang hostile to the boss's within his
// sight range goes on his enemy list.
void scanEnemies(Brain& brain) {
    Gang* own = brain.gang();
    if (own == nullptr) {
        return;
    }
    Gangs& gangs = own->owner();
    for (std::size_t id = 0; id < kGangSlots; ++id) {
        Gang* other = gangs.find(static_cast<int>(id));
        if (other == nullptr || other == own || !(Gangs::enemies(own, other) || Gangs::enemies(other, own))) {
            continue;
        }
        for (Brain* member : other->members()) {
            if (Brain::fightable(*member) && brain.distanceTo(*member) <= sightRangeOf(brain)) {
                brain.addEnemy(*member);
            }
        }
    }
}

// The nearest fightable enemy on `brain`'s list; null when none.
Brain* nearestEnemy(Brain& brain) {
    Brain* nearest = nullptr;
    float best = 0.0F;
    for (Brain* enemy : brain.enemies()) {
        if (!Brain::fightable(*enemy)) {
            continue;
        }
        const float distance = brain.distanceTo(*enemy);
        if (nearest == nullptr || distance < best) {
            nearest = enemy;
            best = distance;
        }
    }
    return nearest;
}

// Whether `brain` may start `kind` on `target` now (`Human_CanStartAttack`).
bool canStart(Brain& brain, const Brain& target, int kind) {
    return canStartAttack(startGuardOf(brain, target, kind), attackerViewOf(brain, &target),
                          targetViewOf(target, brain), kind);
}

// The Start both big bosses share: no threat response or pick-ups, all-round sight, not reachable, the boss
// switches; returns the switches it turned on and saves the turn boost.
std::uint64_t startBoss(Brain& brain, int& savedTurnBoost) {
    brain.setThreatResponse(0);
    brain.setWantsWeapon(false);
    brain.setSight(brain.sightRange(), kAllRound);
    brain.senses().reachable = false;
    const std::uint64_t added = kBossFlags & ~brain.human().flags();
    brain.human().setFlag(kBossFlags, true);
    savedTurnBoost = brain.turnBoost();
    return added;
}

// Their End: threat response 2, the default field of view, reachable, the switches Start turned on off again, the turn
// boost back.
void endBoss(Brain& brain, std::uint64_t added, int savedTurnBoost) {
    brain.setThreatResponse(kThreatAgain);
    brain.setSight(brain.sightRange(), kDefaultView);
    brain.senses().reachable = true;
    brain.human().setFlag(added, false);
    brain.setTurnBoost(savedTurnBoost);
    brain.clearActions();
}

// The throw of what is in hand at `target` (**Coney stand-in** for command `0x10` with an object of set 4: Coney has no
// thrown objects, so only the overhead throw's clip plays, after a turn to him).
void throwAt(ScriptServices* services, Brain& brain, const Brain& target, std::int16_t delayMs) {
    brain.queueAction(TurnAction::toPoint(target.human().position()));
    play(services, brain, boss_anim::kThrow, delayMs);
}

} // namespace

// ---- TiredGoal ----

void TiredGoal::start(Brain& brain) {
    human::Human& human = brain.human();
    m_savedGod = human.hasFlag(human::flag::kGod);
    m_savedNoReact = human.fighter().hitReactionsOff();
    m_savedUnstunnable = human.hasFlag(human::flag::kUnstunnable);
    human.setFlag(human::flag::kGod | human::flag::kUnstunnable, false);
    human.fighter().setHitReactionsOff(false);
    const combat::Health& health = human.fighter().health();
    m_startHealth = health.value();
    m_damageLimit = health.maximum() * m_damagePercent / 100;
    m_deadlineMs = brain.nowMs() + m_fatigueMs;
    brain.clearActions();
    human.stunFor(brain.nowMs(), m_fatigueMs);
}

GoalStatus TiredGoal::process(Brain& brain) {
    human::Human& human = brain.human();
    const std::uint64_t now = brain.nowMs();
    // The break: its start clip, then its hold, until the tactic ends it.
    if (m_breaking) {
        if (!m_breakHoldAtMs.has_value() && brain.actionCount() == 0 && human.animator().flags() == 0) {
            play(m_services, brain, boss_anim::kBreakLoop);
            m_breakHoldAtMs = now;
        }
        return GoalStatus::Stop;
    }
    switch (m_phase) {
    case Phase::Stunned: {
        // Line 0x95 once, then line 8 while he stands stunned.
        say(m_services, brain, m_saidTired ? boss_line::kHurt : boss_line::kTired);
        m_saidTired = true;
        const int lost = m_startHealth - human.fighter().health().value();
        if (now >= m_deadlineMs || lost > m_damageLimit) {
            human.setFlag(human::flag::kGod, m_savedGod);
            human.fighter().setHitReactionsOff(true);
            human.endStun(now);
            m_phase = Phase::Recovering;
        }
        return GoalStatus::Stop;
    }
    case Phase::Recovering:
        // Once he is free: god mode, the recovery line and the rage clip.
        if (human.fighter().victim().stunned() || human.fighter().helpless(human.animator())) {
            return GoalStatus::Stop;
        }
        human.setFlag(human::flag::kGod, true);
        say(m_services, brain, boss_line::kRecover, true);
        play(m_services, brain, boss_anim::kRage);
        m_phase = Phase::Recovered;
        return GoalStatus::Stop;
    case Phase::Recovered:
    default:
        return GoalStatus::Done;
    }
}

void TiredGoal::end(Brain& brain) {
    human::Human& human = brain.human();
    human.setFlag(human::flag::kGod, m_savedGod);
    human.setFlag(human::flag::kUnstunnable, m_savedUnstunnable);
    human.fighter().setHitReactionsOff(m_savedNoReact);
    human.endStun(brain.nowMs());
}

void TiredGoal::startBreak(Brain& brain, float healthPercent) {
    human::Human& human = brain.human();
    human.endStun(brain.nowMs());
    human.fighter().setHitReactionsOff(true);
    human.setFlag(human::flag::kGod, true);
    brain.clearActions();
    play(m_services, brain, boss_anim::kBreak);
    say(m_services, brain, boss_line::kBreak, true);
    m_breaking = true;
    m_breakHoldAtMs.reset();
    human.setHealthPercent(healthPercent);
}

bool TiredGoal::breakDone(const Brain& brain) const {
    return m_breaking && m_breakHoldAtMs.has_value() && brain.nowMs() >= *m_breakHoldAtMs + kBreakHoldMs;
}

void TiredGoal::endBreak(Brain& brain) {
    human::Human& human = brain.human();
    brain.clearActions();
    play(m_services, brain, boss_anim::kBreakEnd);
    human.setFlag(human::flag::kGod, false);
    human.fighter().setHitReactionsOff(false);
    m_breaking = false;
    m_breakHoldAtMs.reset();
}

// ---- BigBrawlerGoal ----

BigBrawlerGoal::BigBrawlerGoal(const BigBrawlerOrder& order, ScriptServices* services, const FlagServices* flags)
    : Goal(GoalType::BigBrawler), m_order(order), m_services(services), m_flags(flags) {}

void BigBrawlerGoal::start(Brain& brain) {
    m_savedFlags = startBoss(brain, m_savedTurnBoost);
    m_state = m_order.stage == 1 ? State::Taunt : State::Engage;
}

void BigBrawlerGoal::end(Brain& brain) { endBoss(brain, m_savedFlags, m_savedTurnBoost); }

std::uint64_t BigBrawlerGoal::fatigueMs() const {
    const auto index = static_cast<std::size_t>(std::clamp(m_order.stage, 1, 3) - 1);
    return static_cast<std::uint64_t>(std::max(0, m_order.tables.fatigue.at(index))) * 1000U;
}

int BigBrawlerGoal::damagePercent() const {
    const auto index = static_cast<std::size_t>(std::clamp(m_order.stage, 1, 3) - 1);
    return m_order.tables.damage.at(index);
}

GoalStatus BigBrawlerGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    switch (m_state) {
    case State::Taunt:
        // Stage 1's opening: the line and the rage clip after 0-750 ms, then straight into the fight.
        say(m_services, brain, boss_line::kTaunt, true);
        play(m_services, brain, boss_anim::kRage, static_cast<std::int16_t>(rollRange(brain.random(), 0, 750)));
        m_state = State::Fight;
        return GoalStatus::Stop;
    case State::Engage:
        return engage(brain);
    case State::Fight:
        return fight(brain);
    case State::FlagCycle:
    default:
        return flagCycle(brain);
    }
}

GoalStatus BigBrawlerGoal::engage(Brain& brain) {
    scanEnemies(brain);
    const Brain* previous = brain.target();
    Brain* enemy = pickBestEnemy(brain, this, score_term::kMelee);
    if (enemy == nullptr) {
        return GoalStatus::Stop;
    }
    // The man he was after is busy: close to the near range and wait a second.
    if (enemy == previous && busy(*enemy)) {
        brain.queueAction(
            std::make_unique<MoveToHumanAction>(static_cast<std::uint32_t>(kBusyWaitMs), brain.meleeNear()));
        return GoalStatus::Stop;
    }
    m_state = State::Fight;
    m_repickAtMs = brain.nowMs() + kRepickMs;
    if (brain.distanceTo(*enemy) > kSpecialShare * brain.meleeFar()) {
        // Far off: run him down.
        auto run = std::make_unique<EngageEnemyGoal>();
        run->setRunIn(kBossRunIn);
        brain.pushGoal(std::move(run));
        return GoalStatus::Again;
    }
    if (canStart(brain, *enemy, kSpecialKind)) {
        queueAttack(brain, kSpecialKind);
        return GoalStatus::Stop;
    }
    // Close by but the special cannot start: now and then a taunt, and a turn to him.
    if (brain.rand100() < kTauntPercent) {
        say(m_services, brain, boss_line::kThrowerTaunt);
    }
    brain.queueAction(TurnAction::toPoint(enemy->human().position()));
    return GoalStatus::Stop;
}

GoalStatus BigBrawlerGoal::fight(Brain& brain) {
    // An object in hand is thrown at the nearest enemy.
    if (m_holding) {
        if (const Brain* enemy = nearestEnemy(brain); enemy != nullptr) {
            throwAt(m_services, brain, *enemy, 0);
            m_holding = false;
        }
        return GoalStatus::Stop;
    }
    // A new target every 4 s.
    if (brain.nowMs() >= m_repickAtMs) {
        m_repickAtMs = brain.nowMs() + kRepickMs;
        scanEnemies(brain);
        static_cast<void>(pickBestEnemy(brain, this, score_term::kMelee));
        m_kind = kNoAttackKind;
    }
    Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target)) {
        m_state = State::Engage;
        return GoalStatus::Stop;
    }
    // A kind, kept until it can be pressed in its reach.
    if (m_kind == kNoAttackKind) {
        m_kind = pickAttackFor(brain, *target, PickFilter::CanUse).value_or(kNoAttackKind);
        if (m_kind == kNoAttackKind) {
            return GoalStatus::Stop;
        }
    }
    const float reach = kindReach(brain, *target, m_kind);
    if (reachDistance(brain, *target) > reach) {
        const float stop = std::max(reach, 2.0F * capsuleRadius(*target) + kMoveBandSlack);
        brain.queueAction(std::make_unique<MoveToHumanAction>(kBossMoveMs, stop));
        return GoalStatus::Stop;
    }
    if (canStart(brain, *target, m_kind)) {
        queueAttack(brain, m_kind);
        m_kind = kNoAttackKind;
    } else {
        m_kind = pickAttackFor(brain, *target, PickFilter::CanStart).value_or(kNoAttackKind);
    }
    return GoalStatus::Stop;
}

GoalStatus BigBrawlerGoal::flagCycle(Brain& brain) {
    const std::optional<world_objects::Placement> flag =
        m_flags != nullptr ? m_flags->flag(m_order.flag) : std::nullopt;
    if (!flag.has_value()) {
        m_state = State::Fight;
        return GoalStatus::Stop;
    }
    const anim::Vec3 point{flag->position[0], flag->position[1], flag->position[2]};
    switch (m_flagStep) {
    case 0:
        // To the flag.
        if (anim::distance(brain.human().position(), point) > kFlagRadius) {
            brain.queueAction(std::make_unique<MoveAction>(
                MoveRequest{.point = point, .radius = kFlagRadius, .gait = kWalkGait, .delayMs = 0}));
            return GoalStatus::Stop;
        }
        m_flagStep = 1;
        return GoalStatus::Stop;
    case 1:
        // The pick-up, facing the players: the next object in his hand.
        if (m_services != nullptr && m_services->player() != nullptr) {
            brain.queueAction(TurnAction::toPoint(m_services->player()->human().position()));
        }
        play(m_services, brain, boss_anim::kPickUp);
        m_holding = true;
        m_flagStep = 2;
        return GoalStatus::Stop;
    default: {
        // Two metres back along the flag's heading, then the fight.
        const anim::Vec3 back = anim::subtract(
            point, anim::scale(human::facing(flag->headingDegrees * std::numbers::pi_v<float> / 180.0F), kStepBack));
        brain.queueAction(std::make_unique<MoveAction>(
            MoveRequest{.point = back, .radius = kFlagRadius, .gait = kWalkGait, .delayMs = 0}));
        m_flagStep = 0;
        m_state = State::Fight;
        return GoalStatus::Stop;
    }
    }
}

float BigBrawlerGoal::adjustEnemyScore(const Brain& scorer, const Brain& candidate, const Brain* /*previous*/,
                                       float score) const {
    const float distance = scorer.distanceTo(candidate);
    const float sight = sightRangeOf(scorer);
    if (distance > sight) {
        return kRuledOut;
    }
    const bool player = candidate.type() == BrainType::Player;
    if (m_state == State::Fight) {
        const float range = kFightRangeShare * scorer.meleeFar();
        const bool diegoLast = scorer.characterClass() == kDiegoClass && m_order.stage == 3;
        if (distance > range && !diegoLast) {
            return kRuledOut;
        }
        return score + kPerMetre * std::max(0.0F, range - distance) + (player ? kFightPlayer : 0.0F) +
               (down(candidate) ? kFightDown : 0.0F);
    }
    return score + kPerMetre * distance + (player && !down(candidate) ? kEngagePlayer : 0.0F);
}

void BigBrawlerGoal::onHit(Brain& brain) {
    if (brain.human().gait() >= human::Gait::Run) {
        say(m_services, brain, boss_line::kHurt);
    }
    if (m_state != State::Fight) {
        return;
    }
    if (++m_hits < kHitsToTire) {
        return;
    }
    m_hits = 0;
    if (m_order.flag != 0) {
        m_state = State::FlagCycle;
        m_flagStep = 0;
    }
    brain.pushGoal(std::make_unique<TiredGoal>(fatigueMs(), damagePercent(), m_services));
}

void BigBrawlerGoal::onAttackWarning(Brain& brain, Brain* attacker) {
    const Goal* top = brain.topGoal();
    if (m_state != State::Fight || (top != nullptr && top->type() == GoalType::Tired) || attacker == nullptr) {
        return;
    }
    const human::Human& them = attacker->human();
    // A grab or tackle he can escape: the counter.
    if (combat::aiCounterFor(static_cast<int>(them.animator().animId())) != combat::anim_id::kNone) {
        brain.press(combat::command::kR1Pressed);
    }
    if (attacker->type() == BrainType::Player && brain.target() != attacker) {
        brain.setTarget(attacker);
    }
    // Too close: shoved off.
    const human::Human& boss = brain.human();
    const bool free = !boss.fighter().inPair() && !boss.fighter().victim().stunned() && !busy(brain);
    if (brain.distanceTo(*attacker) <= kShoveRange && boss.fighter().health().fraction() >= kShoveHealth && free) {
        say(m_services, brain, brain.rand100() < 50 ? boss_line::kShoveA : boss_line::kShoveB, true);
        brain.clearActions();
        play(m_services, brain, boss_anim::kShove);
    }
}

// ---- BigThrowerGoal ----

BigThrowerGoal::BigThrowerGoal(const BigThrowerOrder& order, ScriptServices* services, const FlagServices* flags)
    : Goal(GoalType::BigThrower), m_order(order), m_services(services), m_flags(flags) {}

void BigThrowerGoal::start(Brain& brain) { m_savedFlags = startBoss(brain, m_savedTurnBoost); }

void BigThrowerGoal::end(Brain& brain) { endBoss(brain, m_savedFlags, m_savedTurnBoost); }

GoalStatus BigThrowerGoal::process(Brain& brain) {
    if (brain.actionCount() > 0 || brain.nowMs() < m_waitUntilMs) {
        return GoalStatus::Stop;
    }
    switch (m_state) {
    case State::Taunt:
        say(m_services, brain, boss_line::kThrowerTaunt, true);
        play(m_services, brain, boss_anim::kRage);
        m_state = State::ToFlag;
        return GoalStatus::Stop;
    case State::ToFlag: {
        // Tired after the cycles' throws.
        if (m_order.cycles > 0 && m_throws >= m_order.cycles) {
            m_throws = 0;
            m_halfTaunted = false;
            brain.pushGoal(std::make_unique<TiredGoal>(static_cast<std::uint64_t>(std::max(0, m_order.fatigue)) * 1000U,
                                                       m_order.damage, m_services));
            return GoalStatus::Again;
        }
        const std::optional<world_objects::Placement> flag =
            m_flags != nullptr ? m_flags->flag(m_order.flag) : std::nullopt;
        if (flag.has_value()) {
            const anim::Vec3 point{flag->position[0], flag->position[1], flag->position[2]};
            if (anim::distance(brain.human().position(), point) > kFlagRadius) {
                brain.queueAction(std::make_unique<MoveAction>(
                    MoveRequest{.point = point, .radius = kFlagRadius, .gait = kWalkGait, .delayMs = 0}));
                return GoalStatus::Stop;
            }
        }
        m_state = State::PickUp;
        return GoalStatus::Stop;
    }
    case State::PickUp:
        if (m_services != nullptr && m_services->player() != nullptr) {
            brain.queueAction(TurnAction::toPoint(m_services->player()->human().position()));
        }
        play(m_services, brain, boss_anim::kPickUp);
        m_state = State::Aim;
        return GoalStatus::Stop;
    case State::Aim: {
        scanEnemies(brain);
        Brain* target = brain.target();
        if (target == nullptr || !Brain::fightable(*target)) {
            target = pickBestEnemy(brain, this, score_term::kMelee);
        }
        if (target == nullptr) {
            return GoalStatus::Stop;
        }
        const anim::Vec3 to = anim::subtract(target->human().position(), brain.human().position());
        if (std::fabs(human::wrapAngle(human::headingOf(to) - brain.human().heading())) > kAimAngle) {
            brain.queueAction(TurnAction::toPoint(target->human().position()));
            return GoalStatus::Stop;
        }
        if (!brain.hasLineOfSight(*target)) {
            if (++m_misses >= kAimMisses) {
                m_misses = 0;
                brain.setTarget(nullptr);
            }
            return GoalStatus::Stop;
        }
        m_misses = 0;
        m_state = State::Throw;
        return GoalStatus::Stop;
    }
    case State::Throw:
    default: {
        const Brain* target = brain.target();
        if (target == nullptr) {
            m_state = State::Aim;
            return GoalStatus::Stop;
        }
        constexpr std::int16_t kThrowDelayMs = 100;
        throwAt(m_services, brain, *target, kThrowDelayMs);
        ++m_throws;
        // Half-way through his cycles: a taunt once, and a pause after each throw from then on.
        if (m_order.cycles > 0 && m_throws * 2 >= m_order.cycles) {
            if (!m_halfTaunted) {
                m_halfTaunted = true;
                say(m_services, brain, boss_line::kThrowerTaunt);
            }
            m_waitUntilMs = brain.nowMs() + kHalfWaitMs;
        }
        m_state = State::ToFlag;
        return GoalStatus::Stop;
    }
    }
}

} // namespace coney::ai
