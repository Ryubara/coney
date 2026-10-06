// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/pedestrian_goal.h"

#include <cmath>
#include <limits>
#include <vector>

#include "ai/brain.h"

namespace coney::ai {

namespace {

// A leg's arrival radius, metres: Coney's stand-in.
constexpr float kLegRadius = 1.0F;

} // namespace

int pedestrianGait(int mode) {
    switch (mode) {
    case 2:
        return 3;
    case 3:
        return 4;
    default:
        return 2;
    }
}

void PedestrianGoal::walkTo(Brain& brain, double flag) {
    m_flag = flag;
    m_leg = std::make_unique<MoveToFlagGoal>(
        MoveToFlagOrder{.flag = flag, .gait = pedestrianGait(m_order.mode), .radius = kLegRadius}, *m_flags);
    m_leg->start(brain);
}

void PedestrianGoal::start(Brain& brain) {
    // The node nearest the human whose flag is still there.
    const anim::Vec3 at = brain.human().position();
    std::optional<double> nearest;
    float best = std::numeric_limits<float>::max();
    for (const world_objects::FlagNetNode& node : m_net->all()) {
        if (const std::optional<world_objects::Placement> flag = m_flags->flag(node.flag)) {
            const float d = std::hypot(flag->position[0] - at.x, flag->position[1] - at.y);
            if (d < best) {
                best = d;
                nearest = node.flag;
            }
        }
    }
    if (nearest) {
        walkTo(brain, *nearest);
    }
}

void PedestrianGoal::resume(Brain& brain) {
    if (m_leg) {
        m_leg->resume(brain);
    }
}

GoalStatus PedestrianGoal::process(Brain& brain) {
    if (!m_leg) {
        return GoalStatus::Stop;
    }
    if (m_leg->process(brain) != GoalStatus::Done) {
        return GoalStatus::Stop;
    }
    // Arrived (or the flag is gone): on to a random linked node, or stand.
    const std::vector<double> next = m_flag ? m_net->neighbours(*m_flag) : std::vector<double>{};
    if (next.empty()) {
        m_leg.reset();
        m_flag.reset();
        return GoalStatus::Stop;
    }
    walkTo(brain, next.at(static_cast<std::size_t>(brain.rand100()) % next.size()));
    return GoalStatus::Stop;
}

} // namespace coney::ai
