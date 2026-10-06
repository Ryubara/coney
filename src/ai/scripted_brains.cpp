// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_brains.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ai/address_person_goal.h"
#include "ai/dealer_goal.h"
#include "ai/play_dyn_animation_goal.h"
#include "ai/tactic_crowd.h"
#include "ai/track_human_goal.h"
#include "ai/turn_action.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"

namespace coney::ai {

ScriptedBrains::ScriptedBrains(Brains& brains, const world_objects::WorldFlags& flags,
                               world_objects::ObjectLocator locate)
    : m_owner(&brains), m_flags(&flags), m_locate(std::move(locate)) {
    m_owner->gangs().setScripts(this);
}

ScriptedBrains::~ScriptedBrains() {
    if (m_owner->gangs().scripts() == this) {
        m_owner->gangs().setScripts(nullptr);
    }
}

void ScriptedBrains::bind(double handle, Brain& brain, int gang) {
    m_brains[handle] = &brain;
    brain.setHandle(handle);
    if (gang >= 0) {
        m_owner->gangs().addMember(gang, brain);
    }
}

Brain* ScriptedBrains::brain(double handle) const {
    const auto found = m_brains.find(handle);
    return found == m_brains.end() ? nullptr : found->second;
}

std::optional<anim::Vec3> ScriptedBrains::locate(double handle) const {
    if (const Brain* found = brain(handle); found != nullptr) {
        return found->human().position();
    }
    std::optional<world_objects::Placement> placement = flag(handle);
    if (!placement && m_locate) {
        placement = m_locate(handle);
    }
    if (!placement) {
        return std::nullopt;
    }
    return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
}

void ScriptedBrains::goalMoveToFlag(const script::MoveToFlagCall& call) {
    if (Brain* found = named(call.human); found != nullptr) {
        ai::goalMoveToFlag(*found,
                           MoveToFlagOrder{.flag = call.flag,
                                           .gait = call.gait,
                                           .angleDegrees = call.angle,
                                           .distance = call.distance,
                                           .radius = call.radius,
                                           .intervalMs = call.intervalMs,
                                           .faceFlag = call.faceFlag,
                                           .option = call.option},
                           *this);
    }
}

void ScriptedBrains::actLookAt(const script::LookAtCall& call) {
    if (Brain* found = named(call.human); found != nullptr) {
        found->queueAction(
            TurnAction::lookAt([this](double target) { return locate(target); }, call.target, call.turn, call.delayMs));
    }
}

void ScriptedBrains::goalFight(double human, double target) {
    Brain* fighter = named(human);
    Brain* opponent = named(target);
    if (fighter != nullptr && opponent != nullptr && fighter != opponent) {
        fighter->startFight(*opponent);
    }
}

void ScriptedBrains::brFlush(double human) {
    if (Brain* found = named(human); found != nullptr) {
        found->flush();
    }
}

void ScriptedBrains::brDead(double human, bool dead) {
    if (Brain* found = named(human); found != nullptr) {
        found->setDead(dead);
    }
}

void ScriptedBrains::brSuspend(double human, bool suspended) {
    if (Brain* found = named(human); found != nullptr) {
        found->clearActions();
        found->setSuspended(suspended);
    }
}

void ScriptedBrains::brSetThreatResponse(double human, int response) {
    if (Brain* found = named(human); found != nullptr) {
        found->setThreatResponse(response);
    }
}

void ScriptedBrains::goalPlayDynAnimation(const script::DynAnimationCall& call) {
    if (Brain* found = named(call.human); found != nullptr) {
        ai::goalPlayDynAnimation(*found, *this, call.anim, call.callback, call.option);
    }
}

void ScriptedBrains::goalAddressPerson(const script::AddressPersonCall& call) {
    if (Brain* speaker = named(call.human); speaker != nullptr) {
        ai::goalAddressPerson(*speaker, *this,
                              AddressOrder{.target = call.target,
                                           .approach = call.approach,
                                           .range = call.range,
                                           .scene = call.speech,
                                           .callback = call.callback});
    }
}

void ScriptedBrains::goalTrackHuman(double human, double target, float distance) {
    if (Brain* found = named(human); found != nullptr) {
        ai::goalTrackHuman(*found, *this, m_owner->formations(), target, distance);
    }
}

void ScriptedBrains::goalDealer(const script::DealerCall& call) {
    if (Brain* found = named(call.human); found != nullptr) {
        found->pushGoal(std::make_unique<DealerGoal>(*this, dealerTypeFor(found->characterClass(), call.type),
                                                     call.range, call.runChance, call.dirtyChance, call.option));
    }
}

void ScriptedBrains::brSetNumFollowSlots(double leader, int count, int allowed) {
    if (Brain* found = named(leader); found != nullptr) {
        if (Formation* formation = m_owner->formations().of(*found, true); formation != nullptr) {
            formation->setSlotCount(count, allowed, m_owner->nowMs());
        }
    }
}

void ScriptedBrains::brSetFollowSlot(double leader, int slot, float x, float y, int set) {
    if (Brain* found = named(leader); found != nullptr) {
        if (Formation* formation = m_owner->formations().of(*found, true); formation != nullptr) {
            formation->setSlot(slot, x, y, set, m_owner->nowMs());
        }
    }
}

void ScriptedBrains::brSetFollowSlotSet(double leader, int set) {
    if (Brain* found = named(leader); found != nullptr) {
        if (Formation* formation = m_owner->formations().of(*found, true); formation != nullptr) {
            formation->setSlotSet(set, m_owner->nowMs());
        }
    }
}

void ScriptedBrains::tacticCrowd(int gang, std::string_view callback, bool cheering) {
    m_owner->gangs().setTactic(gang, std::make_unique<TacticCrowd>(std::string(callback), cheering, m_owner->nowMs()));
}

void ScriptedBrains::tacticTrigger(int gang, int what, bool on) {
    Gang* found = m_owner->gangs().find(gang);
    if (found == nullptr) {
        return;
    }
    if (auto* crowd = dynamic_cast<TacticCrowd*>(found->tactic()); crowd != nullptr) {
        crowd->trigger(*found, what, on);
    }
}

void ScriptedBrains::tacticClear(int gang) { m_owner->gangs().setTactic(gang, nullptr); }

int ScriptedBrains::gangCreate(int kind, std::string_view name) { return m_owner->gangs().create(kind, name); }

void ScriptedBrains::gangDelete(int gang) { m_owner->gangs().remove(gang); }

void ScriptedBrains::gangAddMember(int gang, double human) {
    if (Brain* found = named(human); found != nullptr) {
        m_owner->gangs().addMember(gang, *found);
    }
}

void ScriptedBrains::gangBrDead(int gang, bool dead) { m_owner->gangs().setDead(gang, dead); }

void ScriptedBrains::gangBrFlush(int gang) { m_owner->gangs().flush(gang); }

void ScriptedBrains::gangSetThreatResponse(int gang, int response) {
    m_owner->gangs().setThreatResponse(gang, response);
}

void ScriptedBrains::gangMakeEnemies(int a, int b) { m_owner->gangs().makeEnemies(a, b); }

void ScriptedBrains::gangMakeFriends(int a, int b) { m_owner->gangs().makeFriends(a, b); }

void ScriptedBrains::gangSetMsgHandler(int gang, int message, std::string_view handler) {
    m_owner->gangs().setMessageHandler(gang, message, std::string(handler));
}

void ScriptedBrains::gangSuspend(int gang, bool suspended) { m_owner->gangs().suspend(gang, suspended); }

int ScriptedBrains::gangHeadCount(int gang, bool living) {
    const Gang* found = m_owner->gangs().find(gang);
    if (found == nullptr) {
        return 0;
    }
    if (!living) {
        return static_cast<int>(found->members().size());
    }
    return static_cast<int>(std::ranges::count_if(
        found->members(), [](const Brain* member) { return !member->human().fighter().health().depleted(); }));
}

int ScriptedBrains::gangStandingCount(int gang) {
    const Gang* found = m_owner->gangs().find(gang);
    return found != nullptr ? found->standing() : 0;
}

std::optional<world_objects::Placement> ScriptedBrains::flag(double handle) const {
    const world_objects::WorldFlag* found = m_flags->find(handle);
    if (found == nullptr) {
        return std::nullopt;
    }
    return world_objects::Placement{.position = world_objects::WorldFlags::position(*found, m_locate),
                                    .headingDegrees = world_objects::WorldFlags::headingDegrees(*found, m_locate)};
}

void ScriptedBrains::arrived(double /*handle*/, Brain& /*user*/) { ++m_arrivals; }

void ScriptedBrains::schedule(std::string_view function, std::span<const double> args, std::uint32_t delayMs) {
    if (m_scripts != nullptr) {
        m_scripts->schedule(std::string(function), delayMs, args);
    }
}

bool ScriptedBrains::call(std::string_view function, std::span<const double> args) {
    if (m_scripts == nullptr) {
        return false;
    }
    std::vector<script::Value> values;
    values.reserve(args.size());
    for (const double arg : args) {
        values.emplace_back(arg);
    }
    return m_scripts->call(function, values);
}

void ScriptedBrains::playScene(int /*scene*/, Brain& human, std::string_view callback) {
    if (!callback.empty()) {
        const std::array<double, 2> args{human.handle(), 1.0};
        schedule(callback, args, kGoalCallbackDelayMs);
    }
}

} // namespace coney::ai
