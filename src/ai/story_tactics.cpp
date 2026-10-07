// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/story_tactics.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/idle_goals.h"
#include "ai/move_to_flag_goal.h"
#include "ai/script_services.h"
#include "ai/story_goals.h"
#include "ai/tactic_attack.h"
#include "ai/tactic_boss.h"
#include "ai/track_human_goal.h"
#include "human/human.h"

namespace coney::ai {

namespace {

using script::TacticKind;

// The human events the tactics map to codes (docs/research/ai.md#tactic-kinds).
constexpr int kEventSawPlayer = 10;
// How far the followers keep from the leader, by kind, metres.
constexpr float kFollowMoveToFlag = 3.0F;
constexpr float kFollowTravelPath = 1.0F;
constexpr float kFollowWalkinTall = 0.75F;
constexpr float kFollowWander = 4.0F;
// The checks' periods, ms.
constexpr std::uint64_t kWalkinTallCheckMs = 1000;
constexpr std::uint64_t kScoutCheckMs = 200;
constexpr std::uint64_t kUseFlagCheckMs = 1000;
constexpr std::uint64_t kDefendCheckMs = 1500;
constexpr std::uint64_t kHoldTheLineCheckMs = 1750;
constexpr std::uint64_t kPursueCheckMs = 150;
// The gaits: a walk, and UseFlag's gait 3.
constexpr int kWalkGait = 2;
constexpr int kUseFlagGait = 3;
// The travel path's radius at each point, metres.
constexpr float kPathRadius = 0.5F;
// HanginOut without full awareness: the view narrowed by 20°, the sight range to 75 %.
constexpr float kHangOutViewNarrowing = 20.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kHangOutSightShare = 0.75F;
// HoldTheLine: at most this share of the members defend.
constexpr float kDefenderShare = 0.6F;
// How close a defender comes to his line flag, metres (**Coney choice**).
constexpr float kLineRadius = 1.0F;

// Whether `brain` is an AI human's (no player's).
bool isAi(const Brain& brain) { return brain.type() != BrainType::Player; }

// Whether `brain` can take orders: an AI human with health left, not on the ground.
bool canAct(const Brain& brain) {
    return isAi(brain) && Brain::fightable(brain) && brain.human().state() != human::TargetState::Grounded;
}

// Whether any member of `gang` has an enemy with health left.
bool anyEnemy(const Gang& gang) {
    return std::ranges::any_of(gang.members(), [](const Brain* member) {
        return std::ranges::any_of(member->enemies(), [](const Brain* enemy) { return Brain::fightable(*enemy); });
    });
}

// The code event `id` fires: 1 → 5, 10 → 4, 11 → 3, 16 → 6; 0 for the others.
int codeOf(int id) {
    switch (id) {
    case kEventDamaged:
        return kTacDamage;
    case kEventSawPlayer:
        return kTacSeePlayer;
    case kEventEnemyAdded:
        return kTacSeeEnemy;
    case kEventAttackWarning:
        return kTacAttacked;
    default:
        return 0;
    }
}

// Where the flag `handle` is through `flags`; nothing when it is gone.
std::optional<anim::Vec3> flagPoint(FlagServices* flags, double handle) {
    const std::optional<world_objects::Placement> placement = flags != nullptr ? flags->flag(handle) : std::nullopt;
    if (!placement) {
        return std::nullopt;
    }
    return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
}

// Gives each AI member of `gang` that can act an idle goal: it holds its place.
void holdPlaces(Gang& gang) {
    for (Brain* member : gang.members()) {
        if (canAct(*member)) {
            member->pushTacticGoal(std::make_unique<IdleGoal>());
        }
    }
}

} // namespace

// ---- StoryTactic ----

StoryTactic::StoryTactic(const script::TacticCall& call, const TacticServices& services)
    : Tactic(static_cast<int>(call.kind), call.callback), m_call(call), m_services(services) {}

bool StoryTactic::answers(int id) const {
    switch (m_call.kind) {
    case TacticKind::WalkinTall:
    case TacticKind::ManWeaponPile:
    case TacticKind::Pursue:
    case TacticKind::AvoidEnemies:
        return id == kEventDamaged || id == kEventAttackWarning;
    case TacticKind::Defend:
    case TacticKind::HoldTheLine:
    case TacticKind::UseFlag:
    case TacticKind::Scout:
        return false;
    default:
        return codeOf(id) != 0;
    }
}

bool StoryTactic::event(Gang& gang, Brain& /*member*/, const BrainEvent& event) {
    if (answers(event.id)) {
        fireCallback(gang, codeOf(event.id));
    }
    return false;
}

// ---- GroupMoveTactic ----

void GroupMoveTactic::start(Gang& gang) {
    Brain* lead = gang.leader();
    if (lead == nullptr) {
        return;
    }
    m_leader = lead->handle();
    const script::TacticCall& order = call();
    FlagServices* flags = services().flags;
    // The leader's moving goal.
    float follow = kFollowMoveToFlag;
    switch (order.kind) {
    case TacticKind::MoveToFlag:
    case TacticKind::WalkinTall: {
        const bool walk = order.kind == TacticKind::WalkinTall;
        follow = walk ? kFollowWalkinTall : kFollowMoveToFlag;
        if (flags != nullptr) {
            m_moving = goalMoveToFlag(
                *lead, MoveToFlagOrder{.flag = order.flags.at(0), .gait = walk ? kWalkGait : order.gait}, *flags);
        }
        break;
    }
    case TacticKind::TravelPath:
        follow = kFollowTravelPath;
        if (flags != nullptr && !m_path.empty()) {
            lead->pushTacticGoal(std::make_unique<TravelPathGoal>(m_path, order.options.at(script::kTacticLoop) ? 1 : 2,
                                                                  order.options.at(script::kTacticReverse), order.gait,
                                                                  kPathRadius, *flags));
        } else {
            lead->pushTacticGoal(std::make_unique<IdleGoal>());
        }
        break;
    default:
        follow = kFollowWander;
        lead->pushTacticGoal(std::make_unique<IdleGoal>());
        break;
    }
    // The others follow him.
    if (services().scripts == nullptr || services().formations == nullptr) {
        return;
    }
    for (Brain* member : gang.members()) {
        if (member != lead && canAct(*member)) {
            static_cast<void>(goalTrackHuman(*member, *services().scripts, *services().formations, m_leader, follow));
        }
    }
}

int GroupMoveTactic::update(Gang& gang) {
    // The arrival, once: the leader's move-to-flag goal is over.
    if (m_moving) {
        Brain* lead = services().scripts != nullptr ? services().scripts->brain(m_leader) : nullptr;
        if (lead == nullptr || lead->findGoal(GoalType::MoveToFlag) == nullptr) {
            m_moving = false;
            return kTacArrived;
        }
    }
    if (call().kind != TacticKind::WalkinTall) {
        return 0;
    }
    const std::uint64_t now = gang.owner().nowMs();
    if (now < m_nextCheckMs) {
        return 0;
    }
    m_nextCheckMs = now + kWalkinTallCheckMs;
    for (Brain* member : gang.members()) {
        if (const Brain* enemy = nearestGangEnemy(*member, gang.owner());
            enemy != nullptr && member->distanceTo(*enemy) <= call().range) {
            return kTacInRange;
        }
    }
    return 0;
}

// ---- StationTactic ----

bool StationTactic::answers(int id) const {
    // Idle with dynamic idles breaks them off instead of calling back.
    if (call().kind == TacticKind::Idle && call().options.at(script::kTacticLoop)) {
        return false;
    }
    return StoryTactic::answers(id);
}

void StationTactic::start(Gang& gang) {
    const script::TacticCall& order = call();
    FlagServices* flags = services().flags;
    switch (order.kind) {
    case TacticKind::HanginOut:
    case TacticKind::UseFlag: {
        const bool hangOut = order.kind == TacticKind::HanginOut;
        for (Brain* member : gang.members()) {
            if (!canAct(*member)) {
                continue;
            }
            if (hangOut && !order.options.at(script::kTacticAware)) {
                member->setSight(member->sightRange() * kHangOutSightShare,
                                 std::max(member->fieldOfView() - kHangOutViewNarrowing, 0.0F));
            }
            // Walk to the flag, then hold the place there.
            member->pushTacticGoal(std::make_unique<IdleGoal>());
            if (flags != nullptr) {
                static_cast<void>(goalMoveToFlag(*member,
                                                 MoveToFlagOrder{.flag = order.flags.at(0),
                                                                 .gait = hangOut ? kWalkGait : kUseFlagGait,
                                                                 .radius = order.range},
                                                 *flags));
            }
        }
        break;
    }
    default:
        holdPlaces(gang);
        break;
    }
}

int StationTactic::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    switch (call().kind) {
    case TacticKind::UseFlag: {
        if (!m_playerNear && now >= m_nextCheckMs) {
            m_nextCheckMs = now + kUseFlagCheckMs;
            const Brain* player = services().scripts != nullptr ? services().scripts->player() : nullptr;
            const std::optional<anim::Vec3> spot = flagPoint(services().flags, call().flags.at(0));
            if (player != nullptr && spot && anim::distance(player->human().position(), *spot) <= call().range) {
                // They leave the flag.
                m_playerNear = true;
                for (Brain* member : gang.members()) {
                    if (isAi(*member)) {
                        member->popToGoalBase();
                    }
                }
            }
        }
        return m_playerNear ? kTacInRange : 0;
    }
    case TacticKind::Idle:
        if (m_brokenOff && std::ranges::none_of(gang.members(), [](Brain* member) {
                return member->findGoal(GoalType::PlayDynIdle) != nullptr;
            })) {
            m_brokenOff = false;
            return kTacAnimDone;
        }
        return 0;
    case TacticKind::Scout:
        if (now >= m_nextCheckMs) {
            m_nextCheckMs = now + kScoutCheckMs;
            for (Brain* member : gang.members()) {
                if (canAct(*member) && member->findGoal(kMeleeGoal) == nullptr &&
                    std::ranges::any_of(member->enemies(), [](const Brain* e) { return Brain::fightable(*e); })) {
                    giveMelee(*member, gang.owner());
                }
            }
        }
        return 0;
    default:
        return 0;
    }
}

bool StationTactic::event(Gang& gang, Brain& member, const BrainEvent& event) {
    const bool roused = event.id == kEventDamaged || event.id == kEventEnemyAdded || event.id == kEventAttackWarning;
    if (roused && call().kind == TacticKind::Scout && canAct(member)) {
        giveMelee(member, gang.owner());
    }
    if (roused && call().kind == TacticKind::Idle && call().options.at(script::kTacticLoop)) {
        // The dynamic idles end.
        for (Brain* each : gang.members()) {
            while (each->topGoal() != nullptr && each->topGoal()->type() == GoalType::PlayDynIdle) {
                each->popGoal();
            }
        }
        m_brokenOff = true;
    }
    return StoryTactic::event(gang, member, event);
}

// ---- DefendTactic ----

void DefendTactic::start(Gang& gang) {
    if (services().scripts == nullptr || services().formations == nullptr) {
        return;
    }
    for (Brain* member : gang.members()) {
        if (canAct(*member) && member->handle() != call().flags.at(0)) {
            // On top of his own goals (DefendTactic_GiveGoals pops to the base and marks it again).
            member->popToGoalBase();
            member->markGoalBase();
            static_cast<void>(
                goalTrackHuman(*member, *services().scripts, *services().formations, call().flags.at(0), call().range));
        }
    }
}

int DefendTactic::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    if (now < m_nextCheckMs) {
        return 0;
    }
    m_nextCheckMs = now + kDefendCheckMs;
    const Brain* defended = services().scripts != nullptr ? services().scripts->brain(call().flags.at(0)) : nullptr;
    if (defended == nullptr || !Brain::fightable(*defended)) {
        return kTacHumanGone;
    }
    return anyEnemy(gang) ? 0 : kTacNoEnemies;
}

// ---- HoldTheLineTactic ----

void HoldTheLineTactic::start(Gang& gang) {
    FlagServices* flags = services().flags;
    if (flags == nullptr) {
        return;
    }
    const std::optional<anim::Vec3> a = flagPoint(flags, call().flags.at(0));
    const std::optional<anim::Vec3> b = flagPoint(flags, call().flags.at(1));
    const float length = a && b ? anim::distance(*a, *b) : 0.0F;
    const auto members = static_cast<float>(gang.members().size());
    const auto defenders = static_cast<std::size_t>(std::floor(std::min(length, members * kDefenderShare)));
    std::size_t placed = 0;
    for (Brain* member : gang.members()) {
        if (!canAct(*member)) {
            continue;
        }
        member->pushTacticGoal(std::make_unique<IdleGoal>());
        const double flag = placed < defenders ? call().flags.at(placed % 2) : call().flags.at(2);
        static_cast<void>(goalMoveToFlag(*member, MoveToFlagOrder{.flag = flag, .radius = kLineRadius}, *flags));
        ++placed;
    }
}

int HoldTheLineTactic::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    if (now < m_nextCheckMs) {
        return 0;
    }
    m_nextCheckMs = now + kHoldTheLineCheckMs;
    return anyEnemy(gang) ? 0 : kTacNoEnemies;
}

// ---- PursueTactic ----

void PursueTactic::start(Gang& gang) {
    m_target = call().targetGang;
    if (m_target < 0) {
        for (const Brain* member : gang.members()) {
            if (member->target() != nullptr && member->target()->gang() != nullptr) {
                m_target = member->target()->gang()->id();
                break;
            }
        }
    }
    const Gang* target = m_target >= 0 ? gang.owner().find(m_target) : nullptr;
    Brain* leader = target != nullptr ? target->leader() : nullptr;
    if (leader == nullptr) {
        return;
    }
    for (Brain* member : gang.members()) {
        if (canAct(*member)) {
            member->addEnemy(*leader);
            member->setTarget(leader);
            giveMelee(*member, gang.owner());
        }
    }
}

int PursueTactic::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    if (now < m_nextCheckMs) {
        return 0;
    }
    m_nextCheckMs = now + kPursueCheckMs;
    const Gang* target = m_target >= 0 ? gang.owner().find(m_target) : nullptr;
    if (target == nullptr || target->members().empty() || target->leader() == nullptr) {
        return kTacNoEnemies;
    }
    for (Brain* member : gang.members()) {
        for (Brain* other : target->members()) {
            if (Brain::fightable(*other) && member->distanceTo(*other) <= call().range) {
                member->addEnemy(*other);
                return kTacInRange;
            }
        }
    }
    return 0;
}

// ---- makeStoryTactic ----

std::unique_ptr<Tactic> makeStoryTactic(const script::TacticCall& call, std::vector<double> path,
                                        const TacticServices& services) {
    switch (call.kind) {
    case TacticKind::Attack:
    case TacticKind::Confront:
        return nullptr;
    case TacticKind::MoveToFlag:
    case TacticKind::TravelPath:
    case TacticKind::WalkinTall:
    case TacticKind::Wander:
        return std::make_unique<GroupMoveTactic>(call, std::move(path), services);
    case TacticKind::Defend:
        return std::make_unique<DefendTactic>(call, services);
    case TacticKind::HoldTheLine:
        return std::make_unique<HoldTheLineTactic>(call, services);
    case TacticKind::Pursue:
        return std::make_unique<PursueTactic>(call, services);
    case TacticKind::BossDiegoVargas:
        return std::make_unique<BossDiegoVargasTactic>(call, services);
    default:
        return std::make_unique<StationTactic>(call, services);
    }
}

} // namespace coney::ai
