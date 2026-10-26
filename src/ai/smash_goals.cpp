// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/smash_goals.h"

// Stubs until the port: every search finds nothing and the goal ends at once.
namespace coney::ai {

std::optional<anim::Vec3> standPoint(const Brain& /*human*/, anim::Vec3 /*object*/) { return std::nullopt; }

GoalStatus DestroyItemGoal::process(Brain& /*brain*/) { return GoalStatus::Done; }

std::optional<SmashTarget> findSmashTarget(Brain& /*searcher*/, const Brain& /*leader*/, ItemServices& /*items*/,
                                           const ScriptServices& /*services*/, float /*radius*/, float /*range*/) {
    return std::nullopt;
}

std::optional<PaneView> claimPane(Brain& /*human*/, ItemServices& /*items*/, float /*radius*/) { return std::nullopt; }

} // namespace coney::ai
