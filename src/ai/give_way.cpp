// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/give_way.h"

#include <memory>
#include <numbers>
#include <optional>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/sectors.h"
#include "ai/steering.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// A sector's width, radians (45°).
constexpr float kSectorAngle = std::numbers::pi_v<float> / 4.0F;

// The scene's brain that `wanted` is, as one that may be changed: the sector record keeps its humans read-only.
Brain* peerOf(const Brain& owner, const Brain* wanted) {
    if (wanted == nullptr || owner.peers() == nullptr) {
        return nullptr;
    }
    for (const std::unique_ptr<Brain>& peer : *owner.peers()) {
        if (peer.get() == wanted) {
            return peer.get();
        }
    }
    return nullptr;
}

// Whether a mover makes the stander dash: a player going at a jog or faster.
bool dashFrom(const Brain& mover) {
    return mover.type() == BrainType::Player && mover.human().gait() >= human::Gait::Jog;
}

} // namespace

bool isThreat(const Brain& brain, const Brain& other) {
    if (brain.type() == BrainType::Player && other.type() == BrainType::Player) {
        return true;
    }
    return brain.gang() != nullptr && other.gang() != nullptr && Gangs::enemies(brain.gang(), other.gang());
}

bool pushAside(Brain& mover, Brain& stander, anim::Vec3 point, anim::Vec3 step) {
    if (stander.givingWay()) {
        return true;
    }
    if (stander.pushingAside()) {
        return false;
    }
    // Both are marked for the call, so the asking on that it may make cannot come back to them; both marks are
    // cleared after it, as the original does.
    mover.setPushingAside(true);
    stander.setPushingAside(true);
    const bool yes = giveWayTo(mover, stander, point, step);
    mover.setPushingAside(false);
    stander.setPushingAside(false);
    return yes;
}

bool giveWayTo(Brain& mover, Brain& stander, anim::Vec3 point, anim::Vec3 step) {
    // Who may: an AI (or a "dead" brain), no threat either way, and the stander idle under its own control.
    if (stander.type() == BrainType::Player && !stander.dead()) {
        return false;
    }
    if (isThreat(mover, stander) || isThreat(stander, mover) || !stander.human().idleUnderControl()) {
        return false;
    }
    // A free sector: a step there, the actions cleared first.
    if (const std::optional<int> sector = giveWaySector(stander, point, step)) {
        if (!stander.clearActions()) {
            return true;
        }
        const float heading =
            human::wrapAngle(stander.human().heading() + (static_cast<float>(*sector) * kSectorAngle));
        const auto delay = static_cast<std::int16_t>(rollRange(stander.random(), 0, kGiveWayDelayRange - 1));
        stander.queueAction(std::make_unique<GiveWayAction>(stander, heading, mover, dashFrom(mover), delay));
        return true;
    }
    // None: the human in each sector, in the same order, is asked to make room for the stander.
    const int start = giveWayStart(stander.human(), point, step);
    const Sectors& record = stander.sectors(kSectorGiveWayAgeMs);
    for (const int offset : kGiveWayOrder) {
        Brain* other = peerOf(stander, record[start + offset].nearest);
        if (other != nullptr && pushAside(stander, *other, stander.human().position(), step)) {
            return true;
        }
    }
    return false;
}

ActionStatus TakeStepAction::start(Brain& brain) {
    brain.stopMove();
    static_cast<void>(brain.human().enterStepControl(m_heading, brain.turnBoost()));
    return ActionStatus::Running;
}

ActionStatus TakeStepAction::update(Brain& brain) {
    const human::Human& human = brain.human();
    return human.stepControlWaiting() || human.stepHeld() ? ActionStatus::Running : ActionStatus::Done;
}

bool TakeStepAction::abort(Brain& brain) {
    brain.human().leaveStepControl();
    return !brain.human().stepHeld();
}

GiveWayAction::GiveWayAction(Brain& owner, float heading, const Brain& mover, bool boost, std::int16_t delayMs)
    : TakeStepAction(heading, delayMs), m_owner(&owner), m_mover(&mover), m_boost(boost) {
    m_owner->setGivingWay(true);
}

GiveWayAction::~GiveWayAction() { m_owner->setGivingWay(false); }

ActionStatus GiveWayAction::update(Brain& brain) { return TakeStepAction::update(brain); }

ActionStatus GiveWayAction::start(Brain& brain) {
    if (m_boost) {
        m_savedBoost = brain.turnBoost();
        brain.setTurnBoost(m_savedBoost + 1);
        m_raised = true;
    }
    return TakeStepAction::start(brain);
}

bool GiveWayAction::abort(Brain& brain) {
    if (!TakeStepAction::abort(brain)) {
        return false;
    }
    restoreBoost(brain);
    return true;
}

void GiveWayAction::restoreBoost(Brain& brain) {
    if (m_raised) {
        brain.setTurnBoost(m_savedBoost);
        m_raised = false;
    }
}

} // namespace coney::ai
