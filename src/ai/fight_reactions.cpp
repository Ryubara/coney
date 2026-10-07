// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_reactions.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>

#include "ai/attack_action.h"
#include "ai/attack_choice.h"
#include "ai/attack_kinds.h"
#include "ai/attack_views.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/reaction_goals.h"
#include "ai/set_command_action.h"
#include "ai/turn_action.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The kinds the reaction goals name.
constexpr int kGrabStrikeKind = 24;
constexpr int kThrowKind = 25;
constexpr int kThrowKind2 = 29;
constexpr int kStruggleKind = 31;
constexpr int kGroundPunchKind = 35;
constexpr int kMountIdleKind = 36;
constexpr int kGetUpKind = 42;
// A double strike in the grab or on the ground comes this often, percent.
constexpr int kDoublePercent = 40;
// The hand-over chance per CfgGang value 5, percent.
constexpr int kHandOverPercent = 10;
// The grab spin's press waits this long, ms (the set-command action's start delay `0x21`).
constexpr std::int16_t kSpinDelayMs = 0x21;
// The get-up attack's timing: it comes this long after going down, counted up to the cap, ms.
constexpr int kGetUpAfterMs = 1900;
constexpr std::uint64_t kGetUpCapMs = 2000;

// The brain whose human is `human` among those `brain` knows: its target, its enemies, and those holding attack slots
// on it or on its target; null when none.
Brain* brainOf(Brain& brain, const human::Holdable* human) {
    if (human == nullptr) {
        return nullptr;
    }
    const auto is = [human](const Brain* other) { return other != nullptr && &other->human() == human; };
    if (is(brain.target())) {
        return brain.target();
    }
    for (Brain* other : brain.enemies()) {
        if (is(other)) {
            return other;
        }
    }
    for (Brain* other : brain.attackSlots()) {
        if (is(other)) {
            return other;
        }
    }
    if (brain.target() != nullptr) {
        for (Brain* other : brain.target()->attackSlots()) {
            if (is(other)) {
                return other;
            }
        }
    }
    return nullptr;
}

// The brain holding `brain`'s human in a grab, a tackle or a mugging, among those it knows; null when none.
Brain* holderOf(Brain& brain) {
    const auto holds = [&brain](const Brain* other) {
        return other != nullptr && other != &brain && other->human().fighter().held() == &brain.human();
    };
    if (holds(brain.target())) {
        return brain.target();
    }
    for (Brain* other : brain.attackSlots()) {
        if (holds(other)) {
            return other;
        }
    }
    for (Brain* other : brain.enemies()) {
        if (holds(other)) {
            return other;
        }
    }
    return nullptr;
}

// Queues `kind` once, after its chain delay, with `heading` as its stick; twice (the second after the chain delay
// again) when `twice`.
void queueKind(Brain& brain, int kind, bool twice, std::optional<float> heading = std::nullopt) {
    const auto delay = static_cast<std::int16_t>(chainDelayMs(kind, brain.human().animator().anims()));
    brain.queueAction(std::make_unique<AttackAction>(kind, delay, heading));
    if (twice) {
        brain.queueAction(std::make_unique<AttackAction>(kind, delay));
    }
}

} // namespace

float grabMoveHeading(GrabMove move, float heading) {
    constexpr float kQuarter = std::numbers::pi_v<float> / 2.0F;
    switch (move) {
    case GrabMove::Left:
        return human::wrapAngle(heading + kQuarter);
    case GrabMove::Right:
        return human::wrapAngle(heading - kQuarter);
    case GrabMove::Behind:
        return human::wrapAngle(heading + std::numbers::pi_v<float>);
    case GrabMove::Ahead:
    default:
        return human::wrapAngle(heading);
    }
}

int struggleDelayMs(int chainDelayMs, float health, float hurtFraction) {
    const float span = 1.0F - hurtFraction;
    const float strength = span > 0.0F ? std::max(0.0F, (health - hurtFraction) / span) : 1.0F;
    return static_cast<int>(std::lround(static_cast<float>(chainDelayMs) * (1.0F - std::min(strength, 1.0F))));
}

int getUpDelayMs(std::uint64_t downMs) {
    return std::max(0, kGetUpAfterMs - static_cast<int>(std::min(downMs, kGetUpCapMs)));
}

void GrabbingGoal::start(Brain& brain) { m_handOver = brain.rand100() < brain.gangFight().handOver * kHandOverPercent; }

GoalStatus GrabbingGoal::process(Brain& brain) {
    // 1. Not grabbing: done.
    if (!reactionHolds(GoalType::ReactGrabbing, brain.human())) {
        return GoalStatus::Done;
    }
    // 3. The held man is the target; wait for the actions.
    const human::Fighter& fighter = brain.human().fighter();
    Brain* held = brainOf(brain, fighter.held());
    if (held != nullptr && brain.target() != held) {
        brain.setTarget(held);
    }
    if (held == nullptr || brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const std::size_t holders = held->attackSlots().size();
    // 5. A front grab with the flag, the man under two or more attackers: spin him into a rear hold for them.
    if (m_handOver && !fighter.fromRear() && holders >= 2) {
        brain.queueAction(std::make_unique<SetCommandAction>(combat::command::kGrabSpin, kSpinDelayMs));
        return GoalStatus::Stop;
    }
    // 6. A rear grab with the flag holds him up, facing a friend who may hit him.
    if (m_handOver && fighter.fromRear() && holders != 1) {
        const Brain* friendHitter = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const bool players : {true, false}) {
            for (const Brain* other : held->attackSlots()) {
                if (other == &brain || (players && other->type() != BrainType::Player) ||
                    (!players && !held->fightBook().places.holds(other))) {
                    continue;
                }
                if (const float d = brain.distanceTo(*other); d < best) {
                    best = d;
                    friendHitter = other;
                }
            }
            if (friendHitter != nullptr) {
                break;
            }
        }
        if (friendHitter != nullptr) {
            brain.queueAction(TurnAction::toPoint(friendHitter->human().position()));
        }
        return GoalStatus::Stop;
    }
    // 7. A move in the grab.
    const std::optional<int> kind = pickAttackFor(brain, *held, PickFilter::CanStart);
    if (!kind.has_value()) {
        return GoalStatus::Stop;
    }
    if (*kind == kGrabStrikeKind) {
        queueKind(brain, *kind, brain.rand100() < kDoublePercent);
    } else if (*kind == kThrowKind || *kind == kThrowKind2) {
        queueKind(brain, *kind, false, grabMoveHeading(grabMoveDirection(brain), brain.human().heading()));
    } else {
        queueKind(brain, *kind, false);
    }
    return GoalStatus::Stop;
}

void GrabbingGoal::end(Brain& brain) { brain.clearActions(); }

GrabMove grabMoveDirection(Brain& brain) {
    // Rule 4: left, ahead or right.
    return static_cast<GrabMove>(rollRange(brain.random(), 0, 2));
}

GoalStatus MountingGoal::process(Brain& brain) {
    if (!reactionHolds(GoalType::ReactTackling, brain.human())) {
        return GoalStatus::Done;
    }
    Brain* held = brainOf(brain, brain.human().fighter().held());
    if (held != nullptr && brain.target() != held) {
        brain.setTarget(held);
    }
    if (held == nullptr || brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const std::optional<int> kind = pickAttackFor(brain, *held, PickFilter::CanStart);
    if (!kind.has_value() || *kind == kMountIdleKind) {
        return GoalStatus::Stop;
    }
    queueKind(brain, *kind, *kind == kGroundPunchKind && brain.rand100() < kDoublePercent);
    return GoalStatus::Stop;
}

void MountingGoal::end(Brain& brain) { brain.clearActions(); }

GoalStatus HeldGoal::process(Brain& brain) {
    const bool mounted = type() == GoalType::ReactTackled;
    // 1. Free: done.
    if (!reactionHolds(type(), brain.human())) {
        return GoalStatus::Done;
    }
    // 2. A friend's hold is left alone.
    Brain* holder = holderOf(brain);
    if (holder == nullptr || Gangs::friends(holder->gang(), brain.gang())) {
        return GoalStatus::Stop;
    }
    // 4. With threat response 0 (but for a dealer) he only waits.
    if (brain.threatResponse() == 0 && brain.type() != BrainType::Dealer) {
        return GoalStatus::Stop;
    }
    // 5. The holder is the target.
    if (brain.target() != holder) {
        brain.setTarget(holder);
    }
    // 6. Once mugged, no more struggling.
    if (!mounted && holder->human().fighter().combat().mode() == combat::CombatMode::Mugging) {
        m_mugged = true;
    }
    if (m_mugged || brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // 7. The struggle.
    const std::optional<int> kind = pickAttackFor(brain, *holder, PickFilter::CanStart);
    if (!kind.has_value()) {
        return GoalStatus::Stop;
    }
    const human::Fighter& fighter = brain.human().fighter();
    const int chain = chainDelayMs(*kind, brain.human().animator().anims());
    if (mounted || *kind == kStruggleKind) {
        const int delay =
            struggleDelayMs(chain, fighter.health().fraction(), fighter.victim().powerClass().hurtFraction);
        brain.queueAction(std::make_unique<AttackAction>(*kind, static_cast<std::int16_t>(delay)));
    } else {
        const float heading = brain.human().heading();
        const float stick = brain.random().coin() ? heading : human::wrapAngle(heading + std::numbers::pi_v<float>);
        brain.queueAction(std::make_unique<AttackAction>(*kind, static_cast<std::int16_t>(chain), stick));
    }
    return GoalStatus::Stop;
}

void HeldGoal::end(Brain& brain) { brain.clearActions(); }

void GroundedGoal::start(Brain& brain) {
    m_downAtMs = brain.nowMs();
    m_queued = false;
}

GoalStatus GroundedGoal::process(Brain& brain) {
    if (!reactionHolds(GoalType::ReactKnockedDown, brain.human())) {
        return GoalStatus::Done;
    }
    // While someone attacks him, the get-up attack on himself, once.
    if (!m_queued && brain.human().fighter().victim().grounded() && !brain.attackSlots().empty()) {
        const int delay = getUpDelayMs(brain.nowMs() - m_downAtMs);
        brain.queueAction(AttackAction::onSelf(kGetUpKind, static_cast<std::int16_t>(delay)));
        m_queued = true;
    }
    return GoalStatus::Stop;
}

void GroundedGoal::end(Brain& brain) { brain.clearActions(); }

} // namespace coney::ai
