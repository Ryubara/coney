// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/fighter_drop.h"

#include "ai/brain.h"
#include "ai/brains.h"

namespace coney::debug {

// @coney-only a debug aid: the original has no way to drop a human (a stub until the step is built)
ai::Brain* dropTarget(ai::Brains& /*brains*/, const ai::Brain& /*player*/, std::optional<double> /*handle*/) {
    return nullptr;
}

// @coney-only a debug aid: the original has no way to drop a human (a stub until the step is built)
void dropBelowWorld(ai::Brain& /*brain*/) {}

} // namespace coney::debug
