// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/sub_tactics.h"

namespace coney::ai {

void SubTactic::start(Gang& /*gang*/, std::uint64_t /*nowMs*/) {}

bool SubTactic::process(Gang& /*gang*/, std::uint64_t /*nowMs*/) { return false; }

void SubTactic::finish(Gang& /*gang*/) {}

void SubTacticPool::clear() {}

std::optional<std::size_t> SubTacticPool::allocate(std::unique_ptr<SubTactic> /*order*/) { return std::nullopt; }

void SubTacticPool::free(std::size_t /*slot*/) {}

SubTactic* SubTacticPool::at(std::size_t /*slot*/) const { return nullptr; }

std::size_t SubTacticPool::inUse() const { return 0; }

bool StandInOrder::onProcess(Gang& /*gang*/) { return false; }

} // namespace coney::ai
