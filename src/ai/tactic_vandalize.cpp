// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_vandalize.h"

// The wreck tactic, not built yet: it does nothing (the tests come first).

namespace coney::ai {

void VandalizeTactic::giveGoals(Gang& /*gang*/) {}

void VandalizeTactic::start(Gang& /*gang*/) {}

int VandalizeTactic::update(Gang& /*gang*/) { return 0; }

void VandalizeTactic::onDamage(Brain& /*member*/, Brain& /*attacker*/) {}

bool VandalizeTactic::event(Gang& /*gang*/, Brain& /*member*/, const BrainEvent& /*event*/) { return false; }

} // namespace coney::ai
