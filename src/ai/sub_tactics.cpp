// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/sub_tactics.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "ai/gangs.h"

namespace coney::ai {

void SubTactic::start(Gang& gang, std::uint64_t nowMs) {
    // The limit counts from the start. None is never passed: the original's −1 is compared unsigned.
    m_endsAtMs =
        m_limitMs < 0 ? std::numeric_limits<std::uint64_t>::max() : nowMs + static_cast<std::uint64_t>(m_limitMs);
    onStart(gang);
    m_started = true;
}

bool SubTactic::process(Gang& gang, std::uint64_t nowMs) {
    if (!m_started) {
        start(gang, nowMs);
    }
    // Past the limit it is done without asking the class; up to it and at it, the class decides.
    if (nowMs > m_endsAtMs) {
        return true;
    }
    return onProcess(gang);
}

void SubTactic::finish(Gang& gang) { onEnd(gang); }

SubTacticPool::SubTacticPool() {
    // The static initialiser's full reset (its arguments 1 and 0xffff, which only the compiler's start-up call passes):
    // every record free.
    clear();
}

void SubTacticPool::clear() {
    for (std::unique_ptr<SubTactic>& record : m_records) {
        record.reset();
    }
}

std::optional<std::size_t> SubTacticPool::allocate(std::unique_ptr<SubTactic> order) {
    const auto free = std::ranges::find(m_records, nullptr);
    if (free == m_records.end()) {
        return std::nullopt;
    }
    *free = std::move(order);
    return static_cast<std::size_t>(free - m_records.begin());
}

void SubTacticPool::free(std::size_t slot) {
    if (slot < m_records.size()) {
        m_records[slot].reset();
    }
}

SubTactic* SubTacticPool::at(std::size_t slot) const {
    return slot < m_records.size() ? m_records[slot].get() : nullptr;
}

std::size_t SubTacticPool::inUse() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_records, [](const auto& record) { return record != nullptr; }));
}

bool StandInOrder::onProcess(Gang& gang) {
    switch (m_args.kind) {
    case OrderKind::Grab:
        // Coney stand-in: no dog to grab, so done at once, as the real order is when it orders nothing.
        return true;
    case OrderKind::Guard:
        // Coney stand-in: done once past its own deadline, without the closing line the real one says first.
        return gang.owner().nowMs() > m_args.deadlineAtMs;
    default:
        // Coney stand-in: Charge, Leader and Throw order nobody and last their limit.
        return false;
    }
}

} // namespace coney::ai
