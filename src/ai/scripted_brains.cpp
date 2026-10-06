// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_brains.h"

#include <utility>

#include "ai/turn_action.h"

namespace coney::ai {

ScriptedBrains::ScriptedBrains(const world_objects::WorldFlags& flags, world_objects::ObjectLocator locate)
    : m_flags(&flags), m_locate(std::move(locate)) {}

Brain* ScriptedBrains::brain(double handle) const {
    const auto found = m_brains.find(handle);
    return found == m_brains.end() ? nullptr : found->second;
}

std::optional<anim::Vec3> ScriptedBrains::locate(double handle) const {
    if (const Brain* named = brain(handle); named != nullptr) {
        return named->human().position();
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
    if (Brain* named = brain(call.human); named != nullptr) {
        ai::goalMoveToFlag(*named,
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
    if (Brain* named = brain(call.human); named != nullptr) {
        named->queueAction(
            TurnAction::lookAt([this](double target) { return locate(target); }, call.target, call.turn, call.delayMs));
    }
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

} // namespace coney::ai
