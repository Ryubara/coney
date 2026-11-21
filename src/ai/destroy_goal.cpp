// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/destroy_goal.h"

#include "ai/brain.h"

// The Destroy goal, not built yet: it does nothing (the tests come first).

namespace coney::ai {

void DestroyGoal::start(Brain& /*brain*/) {}

void DestroyGoal::resume(Brain& /*brain*/) {}

void DestroyGoal::end(Brain& /*brain*/) {}

bool DestroyGoal::waiting(const Brain& /*brain*/) const { return false; }

void DestroyGoal::pickTarget(Brain& /*brain*/) {}

void DestroyGoal::releaseTarget() {}

void DestroyGoal::armWait(Brain& /*brain*/) {}

GoalStatus DestroyGoal::process(Brain& /*brain*/) { return GoalStatus::Stop; }

} // namespace coney::ai
