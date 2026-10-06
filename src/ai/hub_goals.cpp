// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/hub_goals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <utility>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/move_action.h"
#include "ai/play_anim_action.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The gaits the goals walk and run at.
constexpr int kWalkGait = 2;
constexpr int kRunGait = 4;
// The area walker: mode 3 is the police searcher, which runs to a crime scene that moved more than this, and idles at
// its stops with anim 0x29c, or 0x29e one time in four.
constexpr int kPoliceMode = 3;
constexpr float kSceneMoved = 5.0F;
constexpr int kSearchIdleAnim = 0x29c;
constexpr int kSearchIdleAltAnim = 0x29e;
constexpr int kSearchIdleAltPercent = 25;
// How close a walk counts as arrived, m.
constexpr float kArriveRadius = 0.5F;
// The boxer's wait out of reach, ms, three times in four.
constexpr std::uint64_t kBoxerWaitMs = 2000;
constexpr int kBoxerWaitPercent = 75;
// The grabber reaches his target within this, m (**Coney stand-in** for the grab's reach).
constexpr float kGrabReach = 1.0F;
// The vendor: back to his spot beyond 0.3 m, back to his heading beyond 15°, a pick every 20 brain updates, a player
// watched out to 1.1 times the range, the greet clip's anim id.
constexpr float kSpotSlack = 0.3F;
constexpr float kHeadingSlack = 15.0F * std::numbers::pi_v<float> / 180.0F;
constexpr std::uint64_t kBeckonPeriod = 20;
constexpr float kWatchShare = 1.1F;
constexpr int kGreetAnim = 604;
constexpr int kBeckonPercent = 50;
// The shopkeeper's timings and ranges.
constexpr int kWanderMinMs = 3000;
constexpr int kWanderMaxMs = 5000;
constexpr int kChatMinMs = 10000;
constexpr int kChatMaxMs = 20000;
constexpr float kGreetMinRange = 4.0F;
constexpr int kFightPercent = 70;
constexpr float kGiveInFraction = 0.4F;
constexpr int kKindPhone = 1;
constexpr int kKindFight = 2;
constexpr int kKindCower = 3;
constexpr int kKindCowerNoPlea = 4;
// The crime a phoning shopkeeper reports without an `onPhone` callback: a break-in.
constexpr int kBreakInCrime = 1;

// The distance across the ground between two points.
float flatDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The heading from `from` toward `to`.
float headingTo(anim::Vec3 from, anim::Vec3 to) { return human::headingOf(anim::subtract(to, from)); }

// Queues a walk (or a run) to `point` on `brain`.
void walkTo(Brain& brain, anim::Vec3 point, int gait) {
    brain.queueAction(std::make_unique<MoveAction>(MoveRequest{
        .point = point, .radius = kArriveRadius, .gait = gait, .option = false, .delayMs = 0, .flagKind = false}));
}

// Where a handle is through `services`; nothing without a locator.
std::optional<anim::Vec3> locate(const HubGoalServices& services, double handle) {
    return services.locate ? services.locate(handle) : std::nullopt;
}

// The brain of the human `handle` names, if it is about.
Brain* brainOf(const HubGoalServices& services, double handle) {
    return services.scripts != nullptr ? services.scripts->brain(handle) : nullptr;
}

} // namespace

// ---- AreaWalkerGoal ----

AreaWalkerGoal::AreaWalkerGoal(double flag, int radius, int mode, std::uint32_t durationSeconds, int pauseSeconds,
                               const HubGoalServices& services)
    : Goal(GoalType::AreaWalker), m_flag(flag), m_radius(radius), m_mode(mode), m_durationSeconds(durationSeconds),
      m_pauseSeconds(pauseSeconds), m_services(&services) {}

void AreaWalkerGoal::start(Brain& brain) {
    // The centre: the flag, or where the human stands.
    const std::optional<anim::Vec3> flag = m_flag != 0 ? locate(*m_services, m_flag) : std::nullopt;
    m_centre = flag.value_or(brain.human().position());
    m_firstHeading = brain.human().heading();
    m_endMs = m_durationSeconds > 0 ? brain.nowMs() + (static_cast<std::uint64_t>(m_durationSeconds) * 1000) : 0;
    resume(brain);
}

void AreaWalkerGoal::resume(Brain& brain) {
    brain.clearActions();
    m_walking = false;
}

GoalStatus AreaWalkerGoal::process(Brain& brain) {
    if (m_endMs != 0 && brain.nowMs() >= m_endMs) {
        return GoalStatus::Done;
    }
    // The police follow the crime scene: a scene that moved far is run to, and becomes the centre.
    if (m_mode == kPoliceMode && m_services->crimeScene) {
        if (const std::optional<anim::Vec3> scene = m_services->crimeScene();
            scene && flatDistance(*scene, m_centre) > kSceneMoved) {
            m_centre = *scene;
            brain.clearActions();
            walkTo(brain, m_centre, kRunGait);
            m_walking = true;
            m_pauseUntilMs = 0;
            return GoalStatus::Stop;
        }
    }
    if (brain.actionCount() > 0 || brain.nowMs() < m_pauseUntilMs) {
        return GoalStatus::Stop;
    }
    // Below a radius of 1: stand at the centre, facing the first heading.
    if (m_radius < 1) {
        if (flatDistance(brain.human().position(), m_centre) > kArriveRadius) {
            walkTo(brain, m_centre, kWalkGait);
        } else if (std::abs(human::wrapAngle(brain.human().heading() - m_firstHeading)) > kTurnDoneAngle) {
            brain.queueAction(TurnAction::toHeading(m_firstHeading));
        } else {
            brain.stopMove();
        }
        return GoalStatus::Stop;
    }
    // A walk just ended (or failed): the pause at the stop, with the police's idle.
    if (m_walking) {
        m_walking = false;
        brain.setMoveFailure(MoveFailure::None);
        brain.stopMove();
        const int pauseMs = m_pauseSeconds > 0 ? rollRange(brain.random(), 0, m_pauseSeconds * 1000) : 0;
        m_pauseUntilMs = brain.nowMs() + static_cast<std::uint64_t>(pauseMs);
        if (m_mode == kPoliceMode && m_services->scripts != nullptr) {
            const int anim = brain.rand100() < kSearchIdleAltPercent ? kSearchIdleAltAnim : kSearchIdleAnim;
            brain.queueAction(std::make_unique<PlayAnimAction>(*m_services->scripts, anim, false));
        }
        return GoalStatus::Stop;
    }
    // The next point: a random one within half the radius of the centre.
    const float half = static_cast<float>(m_radius) * 0.5F;
    const float angle = static_cast<float>(rollRange(brain.random(), 0, 359)) * std::numbers::pi_v<float> / 180.0F;
    const float reach = half * std::sqrt(static_cast<float>(rollRange(brain.random(), 0, 1000)) / 1000.0F);
    const anim::Vec3 point = anim::add(m_centre, anim::scale(human::facing(angle), reach));
    walkTo(brain, point, kWalkGait);
    m_walking = true;
    return GoalStatus::Stop;
}

// ---- BoxerGoal ----

BoxerGoal::BoxerGoal(double target, const HubGoalServices& services)
    : Goal(GoalType::Boxer), m_target(target), m_services(&services) {}

GoalStatus BoxerGoal::process(Brain& brain) {
    Brain* target = brainOf(*m_services, m_target);
    if (target == nullptr || target == &brain || !Brain::fightable(*target) || brain.actionCount() > 0 ||
        brain.nowMs() < m_waitUntilMs) {
        return GoalStatus::Stop;
    }
    if (brain.distanceTo(*target) > brain.meleeFar()) {
        // Out of reach: a wait three times in four, else a walk up to it.
        if (brain.rand100() < kBoxerWaitPercent) {
            brain.stopMove();
            m_waitUntilMs = brain.nowMs() + kBoxerWaitMs;
        } else {
            walkTo(brain, target->human().position(), kWalkGait);
        }
        return GoalStatus::Stop;
    }
    // In reach: the fight goal goes over this one.
    brain.setTarget(target);
    static_cast<void>(brain.fight(*target));
    return GoalStatus::Stop;
}

// ---- GrabTargetGoal ----

GrabTargetGoal::GrabTargetGoal(double target, const HubGoalServices& services)
    : Goal(GoalType::GrabTarget), m_target(target), m_services(&services) {}

void GrabTargetGoal::start(Brain& brain) {
    brain.human().script().targetable = false;
    brain.setThreatResponse(0);
}

GoalStatus GrabTargetGoal::process(Brain& brain) {
    Brain* target = brainOf(*m_services, m_target);
    if (target == nullptr || target == &brain || target->human().health().depleted()) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const anim::Vec3 at = target->human().position();
    if (flatDistance(brain.human().position(), at) > kGrabReach) {
        m_holding = false;
        walkTo(brain, at, kWalkGait);
        return GoalStatus::Stop;
    }
    m_holding = true;
    brain.stopMove();
    if (std::abs(human::wrapAngle(headingTo(brain.human().position(), at) - brain.human().heading())) >
        kTurnDoneAngle) {
        brain.queueAction(TurnAction::toPoint(at));
    }
    return GoalStatus::Stop;
}

void GrabTargetGoal::end(Brain& brain) { brain.human().script().targetable = true; }

// ---- PeddlerGoal ----

PeddlerGoal::PeddlerGoal(float range, bool reacts, const std::string& greetAnim, const std::string& idleAnim,
                         const HubGoalServices& services, std::vector<SaidLine>& said)
    : Goal(GoalType::Peddler), m_range(range), m_reacts(reacts), m_clips(!greetAnim.empty() && !idleAnim.empty()),
      m_services(&services), m_said(&said) {}

void PeddlerGoal::start(Brain& brain) {
    m_spot = brain.human().position();
    m_heading = brain.human().heading();
    m_savedThreat = brain.threatResponse();
    brain.setThreatResponse(0);
    m_lastHealth = brain.human().health().value();
}

GoalStatus PeddlerGoal::process(Brain& brain) {
    // Harm to a reacting civilian vendor: the pedestrian reaction against the nearest human, a flight.
    const int health = brain.human().health().value();
    const bool hurt = m_lastHealth >= 0 && health < m_lastHealth;
    m_lastHealth = health;
    if (hurt && m_reacts && brain.type() == BrainType::Civilian && m_services->brains) {
        const Brain* nearest = nullptr;
        for (const Brain* other : m_services->brains()) {
            if (other != &brain && (nearest == nullptr || brain.distanceTo(*other) < brain.distanceTo(*nearest))) {
                nearest = other;
            }
        }
        if (nearest != nullptr) {
            brain.pushGoal(std::make_unique<PedestrianReactionGoal>(nearest->human().position()));
            return GoalStatus::Stop;
        }
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // His spot and his heading.
    if (flatDistance(brain.human().position(), m_spot) > kSpotSlack) {
        walkTo(brain, m_spot, kWalkGait);
        return GoalStatus::Stop;
    }
    brain.stopMove();
    if (brain.nowMs() >= m_lookUntilMs &&
        std::abs(human::wrapAngle(brain.human().heading() - m_heading)) > kHeadingSlack) {
        brain.queueAction(TurnAction::toHeading(m_heading));
        return GoalStatus::Stop;
    }
    if (brain.updates() % kBeckonPeriod != 0 || !m_services->brains) {
        return GoalStatus::Stop;
    }
    // The pick: the nearest passer-by within the range not beckoned yet (a player within 1.1 times it is watched).
    Brain* pick = nullptr;
    for (Brain* other : m_services->brains()) {
        const bool player = other->type() == BrainType::Player;
        const float reach = player ? m_range * kWatchShare : m_range;
        if (other == &brain || brain.distanceTo(*other) > reach ||
            std::ranges::find(m_beckoned, other) != m_beckoned.end()) {
            continue;
        }
        if (player && brain.distanceTo(*other) > m_range) {
            continue;
        }
        if (pick == nullptr || brain.distanceTo(*other) < brain.distanceTo(*pick)) {
            pick = other;
        }
    }
    if (pick == nullptr || !m_clips) {
        return GoalStatus::Stop;
    }
    m_beckoned.push_back(pick);
    const bool player = pick->type() == BrainType::Player;
    if (player || brain.rand100() < kBeckonPercent) {
        m_said->push_back(SaidLine{.human = brain.handle(), .line = player ? "beckon_player" : "beckon"});
    }
    if (m_services->scripts != nullptr) {
        brain.queueAction(std::make_unique<PlayAnimAction>(*m_services->scripts, kGreetAnim, false));
    }
    brain.queueAction(TurnAction::toPoint(pick->human().position()));
    m_lookUntilMs = brain.nowMs() + kBeckonLookMs;
    return GoalStatus::Stop;
}

void PeddlerGoal::end(Brain& brain) { brain.setThreatResponse(m_savedThreat); }

// ---- PlayGenAnimGoal ----

PlayGenAnimGoal::PlayGenAnimGoal(int anim, std::string callback, ScriptServices* services)
    : Goal(GoalType::PlayGenAnim), m_anim(anim), m_callback(std::move(callback)), m_services(services) {}

GoalStatus PlayGenAnimGoal::process(Brain& brain) {
    if (m_queued) {
        return brain.actionCount() == 0 ? GoalStatus::Done : GoalStatus::Stop;
    }
    if (m_anim != 0 && m_anim != 1) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0 || m_services == nullptr) {
        return m_services == nullptr ? GoalStatus::Done : GoalStatus::Stop;
    }
    brain.queueAction(std::make_unique<PlayAnimAction>(*m_services, m_anim == 0 ? kChargeAnim : kFlashAnim, false));
    m_queued = true;
    return GoalStatus::Stop;
}

void PlayGenAnimGoal::end(Brain& brain) {
    if (!m_callback.empty() && m_services != nullptr) {
        const std::array<double, 1> args{brain.handle()};
        m_services->schedule(m_callback, args, kCallbackDelayMs);
    }
}

// ---- ShopkeeperGoal ----

ShopkeeperGoal::ShopkeeperGoal(double store, int kind, bool broom, float range, std::string onDisturbed,
                               std::string onPhone, bool pleads, const HubGoalServices& services,
                               std::vector<SaidLine>& said)
    : Goal(GoalType::Shopkeeper), m_store(store), m_kind(kind), m_broom(broom && kind != kKindPhone), m_range(range),
      m_onDisturbed(std::move(onDisturbed)), m_onPhone(std::move(onPhone)), m_pleads(pleads), m_services(&services),
      m_said(&said) {}

void ShopkeeperGoal::say(const Brain& brain, std::string_view line) {
    m_said->push_back(SaidLine{.human = brain.handle(), .line = std::string(line)});
}

void ShopkeeperGoal::start(Brain& brain) {
    brain.setType(BrainType::CivilianDi);
    brain.setThreatResponse(0);
    if (m_kind == 0) {
        m_kind = brain.rand100() < kFightPercent ? kKindFight : kKindCower;
    }
    m_counter = brain.human().position();
    m_nextWanderMs = brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), kWanderMinMs, kWanderMaxMs));
    m_crimesSeen = m_services->crimeCount ? m_services->crimeCount() : 0;
    m_lastHealth = brain.human().health().value();
}

void ShopkeeperGoal::disturbed(Brain& brain) {
    if (!m_onDisturbed.empty() && m_services->scripts != nullptr) {
        const std::array<double, 1> args{brain.handle()};
        static_cast<void>(m_services->scripts->call(m_onDisturbed, args));
    }
    brain.clearActions();
    switch (m_kind) {
    case kKindPhone:
        m_mood = Mood::Phoning;
        say(brain, "phone_gang");
        if (!m_onPhone.empty() && m_services->scripts != nullptr) {
            const std::array<double, 1> args{brain.handle()};
            static_cast<void>(m_services->scripts->call(m_onPhone, args));
        } else if (m_services->reportBreakIn) {
            m_services->reportBreakIn(brain.human().position());
        }
        break;
    case kKindFight:
        m_mood = Mood::Fighting;
        say(brain, "dead_meat");
        if (Brain* offender = m_services->player ? m_services->player() : nullptr; offender != nullptr) {
            // Fighting means answering: the threat response the start took away comes back for the fight.
            brain.setThreatResponse(2);
            brain.startFight(*offender);
        }
        break;
    default:
        m_mood = Mood::Cowering;
        say(brain, "cower");
        brain.stopMove();
        break;
    }
}

GoalStatus ShopkeeperGoal::process(Brain& brain) {
    // A crime in the store: one reported inside the box since the last look, or harm to himself.
    const int health = brain.human().health().value();
    bool crime = m_lastHealth >= 0 && health < m_lastHealth;
    m_lastHealth = health;
    if (m_services->crimeCount) {
        const std::uint64_t count = m_services->crimeCount();
        if (count != m_crimesSeen) {
            m_crimesSeen = count;
            const std::optional<anim::Vec3> at = m_services->lastCrime ? m_services->lastCrime() : std::nullopt;
            crime = crime || (at && m_services->inBox && m_services->inBox(m_store, *at));
        }
    }
    if (crime && m_mood == Mood::Minding) {
        disturbed(brain);
        return GoalStatus::Stop;
    }
    // A cowering pleader gives in once beaten below 40 %.
    if (m_mood == Mood::Cowering && m_kind == kKindCower && m_pleads &&
        brain.human().health().fraction() < kGiveInFraction) {
        m_mood = Mood::GivenIn;
        say(brain, "mug_grunt");
    }
    if (m_mood != Mood::Minding || brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // The player: greeted within half the range (at least 4 m), chatted to while he stays, and beyond the range the
    // shopkeeper goes back into his store.
    const Brain* player = m_services->player ? m_services->player() : nullptr;
    const float distance = player != nullptr ? brain.distanceTo(*player) : m_range * 2.0F;
    const bool inside = m_services->inBox && m_services->inBox(m_store, brain.human().position());
    if (distance <= std::max(m_range * 0.5F, kGreetMinRange)) {
        if (!m_greeted) {
            m_greeted = true;
            say(brain, "store_greet");
            m_nextChatMs =
                brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), kChatMinMs, kChatMaxMs));
        } else if (brain.nowMs() >= m_nextChatMs) {
            say(brain, "store_chat");
            m_nextChatMs =
                brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), kChatMinMs, kChatMaxMs));
        }
    } else if (distance > m_range) {
        m_greeted = false;
        if (!inside && m_services->inBox) {
            walkTo(brain, m_counter, kWalkGait);
            return GoalStatus::Stop;
        }
    }
    // Every 3-5 s a walk to a flag inside the store, then back to the counter.
    if (brain.nowMs() < m_nextWanderMs) {
        brain.stopMove();
        return GoalStatus::Stop;
    }
    m_nextWanderMs = brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), kWanderMinMs, kWanderMaxMs));
    if (m_away) {
        m_away = false;
        walkTo(brain, m_counter, kWalkGait);
        return GoalStatus::Stop;
    }
    const std::vector<anim::Vec3> spots =
        m_services->flagsInBox ? m_services->flagsInBox(m_store) : std::vector<anim::Vec3>{};
    if (!spots.empty()) {
        const int pick = rollRange(brain.random(), 0, static_cast<int>(spots.size()) - 1);
        walkTo(brain, spots.at(static_cast<std::size_t>(pick)), kWalkGait);
        m_away = true;
    }
    return GoalStatus::Stop;
}

// ---- PedestrianReactionGoal ----

void PedestrianReactionGoal::start(Brain& brain) {
    brain.clearActions();
    m_untilMs = brain.nowMs() + kFleeMs;
}

GoalStatus PedestrianReactionGoal::process(Brain& brain) {
    if (brain.nowMs() >= m_untilMs) {
        brain.stopMove();
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    brain.setMoveFailure(MoveFailure::None);
    anim::Vec3 away = anim::subtract(brain.human().position(), m_from);
    away.z = 0.0F;
    if (anim::length(away) < 0.01F) {
        away = human::facing(brain.human().heading());
    }
    walkTo(brain, anim::add(brain.human().position(), anim::scale(anim::normalise(away), kFleeStep)), kRunGait);
    return GoalStatus::Stop;
}

} // namespace coney::ai
