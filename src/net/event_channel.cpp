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

std::vector<NetEvent> toNetEvents(std::span<const GameEvent> events, const EventSubjectIds& ids) {
    std::vector<NetEvent> out;
    constexpr std::size_t kMaxOrdinals = std::size_t{std::numeric_limits<std::uint16_t>::max()} + 1;
    const std::size_t count = std::min(events.size(), kMaxOrdinals);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const GameEvent& event = events[i];
        const std::uint8_t fields = gameEventFields(event.kind);
        NetEvent net;
        net.kind = event.kind;
        net.ordinal = static_cast<std::uint16_t>(i);
        // Only the fields the kind carries travel; a caller's stray value in another is not sent.
        if ((fields & kEventSubject) != 0 && event.subject != 0.0 && ids) {
            net.subject = ids(event.subject);
        }
        net.id = (fields & kEventId) != 0 ? event.id : 0;
        net.value = (fields & kEventValue) != 0 ? event.value : 0;
        net.extra = (fields & kEventExtra) != 0 ? event.extra : 0;
        if ((fields & kEventPosition) != 0) {
            net.position = event.position;
        }
        if ((fields & kEventRotation) != 0) {
            net.rotation = event.rotation;
        }
        if ((fields & kEventText) != 0) {
            net.text = event.text.substr(0, kMaxGameEventText);
        }
        out.push_back(std::move(net));
    }
    return out;
}

GameEvent toGameEvent(const NetEvent& event) {
    // toNetEvents() sent only the fields the kind carries, so the others are at their defaults already.
    return GameEvent{.kind = event.kind,
                     .subject = 0.0,
                     .id = event.id,
                     .value = event.value,
                     .extra = event.extra,
                     .position = event.position,
                     .text = event.text,
                     .rotation = event.rotation};
}

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

void EventReceiver::discardHeld() {
    if (m_held.empty()) {
        return;
    }
    // The newest held step counts as handed over, so a one-shot of it that comes late is not shown after all.
    const std::uint32_t newest = m_held.rbegin()->first.first;
    if (!m_handedAny || newest > m_handedStep) {
        m_handedStep = newest;
    }
    m_handedAny = true;
    m_stats.discarded += m_held.size();
    m_held.clear();
}

} // namespace coney::net
