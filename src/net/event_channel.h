// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/game_events.h"
#include "net/net_id.h"
#include "net/protocol.h"

// The game events' way from the server's simulation to a client (docs/guides/multiplayer.md#events): the server's
// step's events numbered and split into batches by delivery (EventSender), and the client's side that drops repeats
// and hands each event over once, at its step, in the order it was raised (EventReceiver). Coney's own; the original
// has no network play.

namespace coney::net {

/// Maps a game event's subject (a script handle) to the network id the client knows it by; NetId{} when it has none.
using EventSubjectIds = std::function<NetId(double handle)>;

/// The step's `events` as they travel, ordinals 0, 1, 2, ... in the order raised. A subject is mapped through `ids`
/// (empty: none is mapped, every subject travels as no id). Events past the 65,536th of one step are left out.
[[nodiscard]] std::vector<NetEvent> toNetEvents(std::span<const GameEvent> events, const EventSubjectIds& ids);

/// The event a client takes from `event`: its kind's fields as they travelled, with no subject (0), since a client
/// has no script handle for the server's network ids yet.
[[nodiscard]] GameEvent toGameEvent(const NetEvent& event);

/// One client's outgoing events on the server: each step's events in batches, the reliable ones and the one-shots
/// apart, each batch at most kMaxBatchEvents events and kEventBatchBudgetBytes bytes, numbered in its delivery's own
/// sequence from 1.
class EventSender {
  public:
    /// The batches for `events`, the step `step`'s (ordinals rising): the reliable batches first, then the one-shots'.
    /// None when there are no events.
    [[nodiscard]] std::vector<EventBatch> build(std::uint32_t step, std::span<const NetEvent> events);
    /// The sequence the last batch of a delivery took; 0 before its first.
    [[nodiscard]] std::uint32_t lastSequence(bool reliable) const { return reliable ? m_reliable : m_oneShot; }

  private:
    std::uint32_t m_reliable = 0;
    std::uint32_t m_oneShot = 0;
};

/// An event as a client holds it: the server's step it happened in, and the event.
struct ArrivedEvent {
    std::uint32_t step = 0;
    NetEvent event{};

    friend bool operator==(const ArrivedEvent&, const ArrivedEvent&) = default;
};

/// What EventReceiver::apply() did with a batch.
enum class EventsApplied : std::uint8_t {
    Applied,   ///< its events are held for their step
    Duplicate, ///< a batch of that sequence came before; nothing changed
    Stale,     ///< a one-shots' batch older than the window of sequences remembered; dropped
};

/// The counts an EventReceiver keeps, for the Network page and the tests.
struct EventReceiverStats {
    std::uint64_t batches = 0;    ///< batches applied
    std::uint64_t duplicates = 0; ///< batches dropped as repeats (Duplicate)
    std::uint64_t stale = 0;      ///< one-shots' batches dropped as too old (Stale)
    std::uint64_t late = 0;       ///< one-shot events dropped because their step had already been handed over
    std::uint64_t handed = 0;     ///< events handed over by take()
    std::uint64_t discarded = 0;  ///< events held and dropped by discardHeld()
};

/// A client's incoming events: the batches it decodes, each event held until take() reaches its step, then handed
/// over once, in step order and within a step in the order the server raised them. A repeated batch (the transport
/// or a link conditioner may deliver one twice) is dropped by its sequence; a one-shot whose step has already been
/// handed over is dropped as late (a sound out of time is worse than none), a reliable event is handed over at the
/// next take() however late it comes.
class EventReceiver {
  public:
    /// How many one-shots' sequences back a repeat is still recognised.
    static constexpr std::uint32_t kOneShotWindow = 64;

    /// Takes in a decoded batch. Fails with Invalid when it holds an event already held for its step and ordinal
    /// under another sequence (a server that numbers twice), which the session treats as a protocol error.
    [[nodiscard]] std::expected<EventsApplied, Error> apply(const EventBatch& batch);
    /// The events held whose step is at most `step`, oldest first; each is handed over once.
    [[nodiscard]] std::vector<ArrivedEvent> take(std::uint32_t step);
    /// Drops every event held (a reliable one too), as a client does when the level they belong to ends: none is handed
    /// over later, and a one-shot that comes late for a step held is dropped as late. Counts them in
    /// EventReceiverStats::discarded.
    void discardHeld();
    /// How many events are held, waiting for their step.
    [[nodiscard]] std::size_t pending() const { return m_held.size(); }
    /// The counts so far.
    [[nodiscard]] const EventReceiverStats& stats() const { return m_stats; }

  private:
    // Whether the one-shots' sequence `sequence` was seen; records it when not. Stale ones count as seen.
    [[nodiscard]] EventsApplied seeOneShot(std::uint32_t sequence);

    std::map<std::pair<std::uint32_t, std::uint16_t>, NetEvent> m_held; // by (step, ordinal)
    std::uint32_t m_reliable = 0;                                       // the newest reliable sequence applied
    std::uint32_t m_oneShotTop = 0;                                     // the newest one-shots' sequence seen
    std::uint64_t m_oneShotSeen = 0;                                    // bit i: sequence m_oneShotTop - i was seen
    bool m_handedAny = false;                                           // take() has handed over a step
    std::uint32_t m_handedStep = 0;                                     // the newest step take() was asked for
    EventReceiverStats m_stats;
};

} // namespace coney::net
