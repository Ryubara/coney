// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_crowd.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "ai/attack_kinds.h"
#include "ai/gangs.h"
#include "ai/idle_goals.h"
#include "ai/play_anim_action.h"
#include "ai/script_services.h"

namespace coney::ai {

namespace {

// The crowd's ticks: every 1-2 s, and its cheers every 2 s, ms.
constexpr int kTickMinMs = 1000;
constexpr int kTickMaxMs = 2000;
constexpr std::uint64_t kCheerPeriodMs = 2000;
// A watcher gestures at this chance, percent (rand100 < 51).
constexpr int kWatchGestureChance = 51;
// A watcher's spectate goal lasts 4-6 s.
constexpr int kSpectateMinMs = 4000;
constexpr int kSpectateMaxMs = 6000;
// Violence above this strength stirs a cheering crowd; its chance is the strength, at most 100 %.
constexpr int kViolenceThreshold = 29;
// The reactions' start delays: 0-750 ms when switched, 200-400 ms for violence.
constexpr int kReactDelayMaxMs = 750;
constexpr int kViolenceDelayMinMs = 200;
constexpr int kViolenceDelayMaxMs = 400;
// The events it answers (docs/research/ai.md#tactics).
constexpr int kEventViolence = 0x14;
constexpr int kEventReseat = 0x13;
constexpr int kEventReseatToo = 0x16;
// What its callback gets for a member warned of an attack.
constexpr int kCallbackWarned = 6;

// Whether `member` is free to react: on its feet with health left (**Coney choice** standing in for `0x00223c30`).
bool freeToReact(const Brain& member) {
    return !member.human().fighter().health().depleted() && member.human().state() == human::TargetState::Standing;
}

} // namespace

TacticCrowd::TacticCrowd(std::string callback, bool cheering, std::uint64_t nowMs)
    : Tactic(kCrowdTactic, std::move(callback)), m_cheering(cheering), m_nextTickMs(nowMs + kCheerPeriodMs) {}

void TacticCrowd::start(Gang& gang) {
    seat(gang);
    if (!m_cheering) {
        return;
    }
    // A cheering crowd takes no hit reactions and never fights.
    for (Brain* member : gang.members()) {
        member->human().fighter().setHitReactionsOff(true);
        member->setThreatResponse(0);
    }
    m_nextCheerMs = gang.owner().nowMs() + kCheerPeriodMs;
}

int TacticCrowd::update(Gang& gang) {
    const std::uint64_t now = gang.owner().nowMs();
    if (m_nextTickMs < now) {
        m_nextTickMs = now + static_cast<std::uint64_t>(rollRange(gang.owner().random(), kTickMinMs, kTickMaxMs));
        if (!m_cheering) {
            // The first free watcher that passes the roll gestures.
            ScriptServices* scripts = gang.owner().scripts();
            for (Brain* member : gang.members()) {
                if (member->human().animator().flags() == 0 &&
                    rollRange(gang.owner().random(), 0, 99) < kWatchGestureChance) {
                    if (scripts != nullptr) {
                        static_cast<void>(scripts->playClip(*member, kCrowdWatchAnim));
                    }
                    break;
                }
            }
        } else {
            react(gang, false, true);
        }
    }
    // A cheering crowd's next member in turn cheers (its cheer idles are not built).
    if (m_cheering && m_nextCheerMs < now) {
        m_nextCheerMs = now + kCheerPeriodMs;
        const int living = static_cast<int>(std::ranges::count_if(
            gang.members(), [](const Brain* m) { return !m->human().fighter().health().depleted(); }));
        if (living > 0) {
            m_cheerTurn = (m_cheerTurn + 1) % living;
        }
    }
    return 0;
}

bool TacticCrowd::event(Gang& gang, Brain& member, const BrainEvent& event) {
    switch (event.id) {
    case kEventAttackWarning:
        fireCallback(gang, kCallbackWarned);
        return false;
    case kEventViolence:
        if (m_cheering && event.value > kViolenceThreshold && freeToReact(member) && member.actionCount() == 0 &&
            rollRange(gang.owner().random(), 0, 99) <= std::min(event.value, 100)) {
            queueReaction(gang, member, kCrowdReactAnim, kViolenceDelayMinMs, kViolenceDelayMaxMs);
        }
        return true;
    case kEventReseat:
    case kEventReseatToo:
        seat(gang);
        return false;
    default:
        return false;
    }
}

void TacticCrowd::trigger(Gang& gang, int what, bool on) {
    if (what == 0) {
        m_reactions = on;
    } else if (what == 1) {
        react(gang, true, on);
    }
}

void TacticCrowd::seat(Gang& gang) {
    for (Brain* member : gang.members()) {
        if (member->human().fighter().health().depleted()) {
            continue;
        }
        member->flush();
        if (m_cheering) {
            member->pushGoal(std::make_unique<IdleGoal>());
        } else {
            member->pushGoal(std::make_unique<SpectateGoal>(kSpectateMinMs, kSpectateMaxMs));
        }
    }
}

void TacticCrowd::react(Gang& gang, bool all, bool on) {
    if (!m_reactions && !all) {
        return;
    }
    for (Brain* member : gang.members()) {
        if (!freeToReact(*member) || member->actionCount() > 0) {
            continue;
        }
        int first = kCrowdReactAnim;
        if (rollRange(gang.owner().random(), 0, 99) < 50 && !all) {
            first = kCrowdReactAltAnim;
        }
        if (all && !on) {
            first = kCrowdReactOffAnim;
        }
        queueReaction(gang, *member, first, 0, kReactDelayMaxMs);
    }
}

void TacticCrowd::queueReaction(Gang& gang, Brain& member, int first, int minDelayMs, int maxDelayMs) {
    ScriptServices* scripts = gang.owner().scripts();
    if (scripts == nullptr) {
        return;
    }
    const auto delay = static_cast<std::int16_t>(rollRange(gang.owner().random(), minDelayMs, maxDelayMs));
    member.queueAction(std::make_unique<PlayAnimAction>(*scripts, first, true, delay));
    member.queueAction(std::make_unique<PlayAnimAction>(*scripts, kCrowdCheerAnim, false));
}

} // namespace coney::ai
