// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/event_channel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/game_events.h"
#include "net/protocol.h"

namespace coney::net {

namespace {

// Bits an EventBatch takes before its events with `sequence` and `step`, its message id included.
std::size_t batchHeaderBits(std::uint32_t sequence, std::uint32_t step) {
    const auto varUintBits = [](std::uint64_t value) {
        std::size_t groups = 1;
        while (value >= 0x80U) {
            value >>= 7U;
            ++groups;
        }
        return groups * 8;
    };
    return 8 + 1 + varUintBits(sequence) + varUintBits(step) + 8;
}

} // namespace

// Stub for the strengthened test's first run; the next commit restores it.
std::vector<NetEvent> toNetEvents(std::span<const GameEvent> /*events*/, const EventSubjectIds& /*ids*/) { return {}; }

std::vector<EventBatch> EventSender::build(std::uint32_t step, std::span<const NetEvent> events) {
    std::vector<EventBatch> batches;
    // Reliable first: a message and the sound it comes with arrive in the order a player would expect either way.
    for (const bool reliable : {true, false}) {
        std::uint32_t& sequence = reliable ? m_reliable : m_oneShot;
        EventBatch* open = nullptr;
        std::size_t bits = 0;
        for (const NetEvent& event : events) {
            if (reliableGameEvent(event.kind) != reliable) {
                continue;
            }
            const std::size_t cost = eventBits(event);
            const bool full = open != nullptr && (open->events.size() == kMaxBatchEvents ||
                                                  (bits + cost + 7) / 8 > kEventBatchBudgetBytes);
            if (open == nullptr || full) {
                ++sequence;
                batches.push_back(EventBatch{.reliable = reliable, .sequence = sequence, .step = step, .events = {}});
                open = &batches.back();
                bits = batchHeaderBits(sequence, step);
            }
            open->events.push_back(event);
            bits += cost;
        }
    }
    return batches;
}

EventsApplied EventReceiver::seeOneShot(std::uint32_t sequence) {
    if (sequence > m_oneShotTop) {
        const std::uint32_t ahead = sequence - m_oneShotTop;
        m_oneShotSeen = ahead >= 64 ? 0 : m_oneShotSeen << ahead;
        m_oneShotSeen |= 1U;
        m_oneShotTop = sequence;
        return EventsApplied::Applied;
    }
    const std::uint32_t back = m_oneShotTop - sequence;
    if (back >= kOneShotWindow) {
        return EventsApplied::Stale;
    }
    const std::uint64_t bit = std::uint64_t{1} << back;
    if ((m_oneShotSeen & bit) != 0) {
        return EventsApplied::Duplicate;
    }
    m_oneShotSeen |= bit;
    return EventsApplied::Applied;
}

std::expected<EventsApplied, Error> EventReceiver::apply(const EventBatch& batch) {
    EventsApplied applied = EventsApplied::Applied;
    if (batch.reliable) {
        // Reliable batches come in order; one at or below the newest applied is a repeat.
        if (batch.sequence <= m_reliable) {
            applied = EventsApplied::Duplicate;
        } else {
            m_reliable = batch.sequence;
        }
    } else {
        applied = seeOneShot(batch.sequence);
    }
    if (applied == EventsApplied::Duplicate) {
        ++m_stats.duplicates;
        return applied;
    }
    if (applied == EventsApplied::Stale) {
        ++m_stats.stale;
        return applied;
    }
    ++m_stats.batches;
    const bool handedOver = m_handedAny && batch.step <= m_handedStep;
    for (const NetEvent& event : batch.events) {
        if (handedOver && !batch.reliable) {
            ++m_stats.late;
            continue;
        }
        const auto [where, inserted] = m_held.try_emplace({batch.step, event.ordinal}, event);
        if (!inserted) {
            return fail(ErrorCode::Invalid, "network event repeats a step's ordinal under another sequence");
        }
    }
    return applied;
}

std::vector<ArrivedEvent> EventReceiver::take(std::uint32_t step) {
    std::vector<ArrivedEvent> out;
    auto it = m_held.begin();
    while (it != m_held.end() && it->first.first <= step) {
        out.push_back(ArrivedEvent{.step = it->first.first, .event = std::move(it->second)});
        it = m_held.erase(it);
    }
    if (!m_handedAny || step > m_handedStep) {
        m_handedStep = step;
    }
    m_handedAny = true;
    m_stats.handed += out.size();
    return out;
}

} // namespace coney::net
