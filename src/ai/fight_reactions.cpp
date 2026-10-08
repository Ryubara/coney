// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_reactions.h"

#include <algorithm>
#include <array>
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
#include "ai/script_services.h"
#include "ai/sectors.h"
#include "ai/set_command_action.h"
#include "ai/story_tactics.h"
#include "ai/tactic_domination.h"
#include "ai/turn_action.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "scripting/story_bindings.h"

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

// The class whose held man struggles whatever his threat response, with a hurt fraction of 0 (type 221: the dogs).
constexpr int kDogClass = 221;

// A grabber shows his man to a friendly player this close, metres, for this long, ms.
constexpr float kPresentRange = 3.0F;
constexpr std::uint64_t kPresentMs = 3000;

// Whether `brain`'s human is busy (`Human_IsBusy`: his record bits and his state).
bool busy(const Brain& brain) { return humanBusy(brain.human()); }

// The throw away from something in the grabber's sector `k` (Coney's numbering, the mirror of the game's): ahead of
// him (0, 1, 7) behind, on his left (2; the game's 6) right, behind him (3-5) ahead, on his right (6; the game's 2)
// left.
GrabMove awayFrom(int k) {
    switch (wrapSector(k)) {
    case 2:
        return GrabMove::Right;
    case 3:
    case 4:
    case 5:
        return GrabMove::Ahead;
    case 6:
        return GrabMove::Left;
    default:
        return GrabMove::Behind;
    }
}

// One of Grabbing_PickMove's side tests: a sector of the grabber's record or the man's, and the move toward it.
struct SideTest {
    bool grabbers;
    int sector;
    GrabMove move;
};
// In the original's order: behind the man, his left (the game's 6, Coney's 2), his right (the game's 2, Coney's 6),
// behind the grabber. The man faces the grabber, so his left is the grabber's right.
constexpr std::array<SideTest, 4> kSideTests{{{false, 4, GrabMove::Ahead},
                                              {false, 2, GrabMove::Right},
                                              {false, 6, GrabMove::Left},
                                              {true, 4, GrabMove::Behind}}};

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

void GrabbingGoal::start(Brain& brain) {
    m_handOver = brain.rand100() < brain.gangFight().handOver * kHandOverPercent;
    // A man holding a flag or guarding someone does not hand his man over.
    if (brain.findGoal(kHoldFlagGoal) != nullptr || brain.findGoal(GoalType::TrackHuman) != nullptr) {
        m_handOver = false;
    }
}

bool GrabbingGoal::presenting(Brain& brain) {
    const Brain* player = brain.nearestPlayer();
    if (player == nullptr || !Gangs::friends(player->gang(), brain.gang()) || busy(*player) ||
        brain.distanceTo(*player) > kPresentRange) {
        return false;
    }
    const std::uint64_t now = brain.nowMs();
    if (!m_presentUntilMs.has_value()) {
        m_presentUntilMs = now + kPresentMs;
    }
    return now < *m_presentUntilMs;
}

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
    // 4. Presenting him: a rear grab shown to a friendly player close by, for up to 3 s.
    const bool shown = fighter.fromRear() && presenting(brain);
    // 5. A front grab with the flag, the man under two or more attackers: spin him into a rear hold for them.
    if (m_handOver && !fighter.fromRear() && holders >= 2) {
        brain.queueAction(std::make_unique<SetCommandAction>(combat::command::kGrabSpin, kSpinDelayMs));
        return GoalStatus::Stop;
    }
    // 6. A rear grab with the flag, or one shown, holds him up, facing a friend who may hit him.
    if (fighter.fromRear() && ((m_handOver && holders != 1) || shown)) {
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
        queueKind(brain, *kind, false, grabMoveHeading(grabMoveDirection(brain, *held), brain.human().heading()));
    } else {
        queueKind(brain, *kind, false);
    }
    return GoalStatus::Stop;
}

void GrabbingGoal::end(Brain& brain) { brain.clearActions(); }

GrabMove grabMoveDirection(Brain& brain, Brain& held) {
    // Rule 1: away from his HoldFlag goal's flag.
    if (const auto* hold = static_cast<const HoldFlagGoal*>(brain.findGoal(kHoldFlagGoal)); hold != nullptr) {
        if (const std::optional<anim::Vec3> flag = hold->point()) {
            return awayFrom(sectorOf(brain.human(), *flag));
        }
    }
    // Rule 2: into a wall next to them, else into men not his friends.
    if (brain.human().fighter().victim().powerClass().throwsAtWalls) {
        Sectors& mine = brain.sectors(kSectorAgeMs);
        Sectors& his = held.sectors(kSectorAgeMs);
        for (const bool walls : {true, false}) {
            std::array<GrabMove, 4> choices{};
            std::size_t count = 0;
            for (const SideTest& test : kSideTests) {
                Sectors& record = test.grabbers ? mine : his;
                const Brain& owner = test.grabbers ? brain : held;
                // The men: the sector's flag 1 and its nearest man not a friend. As the original does, the test
                // behind the grabber reads his flag with the man's sector-4 human.
                const Brain* other = his[test.sector].nearest;
                const bool occupied = (record[test.sector].flags & sector_flag::kOccupied) != 0;
                const bool hit = walls ? record.wall(owner, test.sector)
                                       : occupied && other != nullptr && other != &brain &&
                                             !Gangs::friends(other->gang(), brain.gang());
                if (hit) {
                    choices.at(count++) = test.move;
                }
            }
            if (count > 0) {
                return choices.at(static_cast<std::size_t>(rollRange(brain.random(), 0, static_cast<int>(count) - 1)));
            }
        }
    }
    // Rule 3: away from the human his gang's Defend tactic defends.
    if (const Gang* gang = brain.gang(); gang != nullptr && gang->tactic() != nullptr &&
                                         gang->tactic()->type() == static_cast<int>(script::TacticKind::Defend)) {
        const auto* defend = static_cast<const DefendTactic*>(gang->tactic());
        ScriptServices* scripts = gang->owner().scripts();
        if (const Brain* defended = scripts != nullptr ? scripts->brain(defend->defended()) : nullptr) {
            return awayFrom(sectorOf(brain.human(), defended->human().position()));
        }
    }
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
    // 4. With threat response 0 (but for a dealer or a class-221 human) he only waits.
    const bool dog = brain.characterClass() == kDogClass;
    if (brain.threatResponse() == 0 && brain.type() != BrainType::Dealer && !dog) {
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
        const int delay = struggleDelayMs(chain, fighter.health().fraction(),
                                          dog ? 0.0F : fighter.victim().powerClass().hurtFraction);
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
