// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>

#include "core/error.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"

// Snapshots: what each client is told of the humans, as deltas against a snapshot it has acknowledged, in Quake 3's
// way, within a budget of bytes that a priority accumulator fills. The server keeps a SnapshotSender per client and
// the client a SnapshotReceiver. Coney's own; the original has no network play (docs/guides/snapshots.md).

namespace coney::net {

/// The most bytes a Snapshot's packet takes unless told otherwise: one datagram's payload below every common path's
/// MTU (docs/guides/network-architecture.md#transport-needs).
inline constexpr std::size_t kSnapshotBudgetBytes = 1200;

/// The humans a snapshot leaves a client holding, by id: each human's fields as words. A map, so it is walked in id
/// order on every machine.
using SnapshotHumans = std::map<NetId, HumanWords>;

/// A human's fields as words (fieldWord() of each), from its values in HumanField order. A value of the wrong
/// alternative or an Integer out of its range is a programmer error, as for writeField().
/// @coney-only network play; the original has none
[[nodiscard]] HumanWords humanWords(std::span<const FieldValue, kHumanFieldCount> values);

/// The values a human's words stand for (fieldValue() of each). The words of a decoded snapshot are always valid; a
/// word over its field's range here is a programmer error (CONEY_ASSERT).
/// @coney-only network play; the original has none
[[nodiscard]] std::array<FieldValue, kHumanFieldCount> humanValues(const HumanWords& words);

/// One human as the server's game hands it to a snapshot.
/// @coney-only network play; the original has none
struct SnapshotHuman {
    NetId id;         ///< valid(), and once in a step's list
    HumanWords words; ///< humanWords() of its fields this step
    /// How much its priority grows each snapshot it has a change to send and is left out: 1 for most, more for what
    /// the client should see sooner (near its player, on screen, fighting him). With 0 it goes only where the budget
    /// has room left after the others.
    std::uint32_t priority = 1;
};

/// The server's side of one client's snapshots: keeps the last kSnapshotHistory it sent and what each leaves the
/// client holding, writes each new one as a delta against the newest the client has acknowledged (in full when it
/// has acknowledged none still kept), and fits it in a budget of bytes in order of a priority that grows each
/// snapshot a human's change waits.
///
/// A removal always goes first. A human that does not fit stays as the baseline has it (not at all, when new) and
/// waits with a higher priority; one sent has its priority back at 0. Plain data with no clock and no randomness:
/// the same calls give the same bytes. Simulation-side state, owned with the client's slot by the session.
/// @coney-only network play; the original has none
class SnapshotSender {
  public:
    /// A sender whose snapshots take at most `budgetBytes` bytes as packets, at least enough for the header and one
    /// full human (CONEY_ASSERT).
    explicit SnapshotSender(std::size_t budgetBytes = kSnapshotBudgetBytes);

    /// The next snapshot of `humans` (the humans this client needs at the server's `step`, in any order): a delta
    /// against the newest acknowledged snapshot still kept, or full. Remembers what it leaves the client holding.
    [[nodiscard]] Snapshot build(std::uint32_t step, std::span<const SnapshotHuman> humans);

    /// The client has decoded snapshot `sequence`; later snapshots may take it as their baseline. False, and nothing
    /// changes, when it is not newer than the one acknowledged before (a late or repeated acknowledgement) or no
    /// longer kept. Fails with Invalid when the sender never sent it, since a client that names it is broken or lying.
    [[nodiscard]] std::expected<bool, Error> acknowledge(std::uint32_t sequence);

    /// The newest sequence the client has acknowledged; 0 for none.
    [[nodiscard]] std::uint32_t acknowledged() const { return m_acknowledged; }
    /// The last sequence built; 0 before the first.
    [[nodiscard]] std::uint32_t sequence() const { return m_sequence; }
    /// What snapshot `sequence` leaves the client holding, while it is kept; null when it is not.
    [[nodiscard]] const SnapshotHumans* holding(std::uint32_t sequence) const;
    /// A human's priority now: what it has grown to while its change waited; 0 when it has none waiting.
    [[nodiscard]] std::uint64_t priority(NetId id) const;
    /// The packet budget, bytes.
    [[nodiscard]] std::size_t budgetBytes() const { return m_budgetBytes; }

  private:
    // One snapshot sent: its sequence (0 for an empty place) and what it leaves the client holding.
    struct Sent {
        std::uint32_t sequence = 0;
        SnapshotHumans holding;
    };

    std::size_t m_budgetBytes = kSnapshotBudgetBytes;
    std::uint32_t m_sequence = 0;
    std::uint32_t m_acknowledged = 0;
    std::array<Sent, kSnapshotHistory> m_sent{}; // by sequence % kSnapshotHistory
    std::map<NetId, std::uint64_t> m_priority;   // the humans whose change is waiting, and how long
};

/// What SnapshotReceiver::apply() made of a snapshot.
/// @coney-only network play; the original has none
enum class SnapshotApplied : std::uint8_t {
    Applied,    ///< decoded: it is the newest, and should be acknowledged
    Stale,      ///< not newer than the newest decoded: ignored
    NoBaseline, ///< its baseline is not held (never decoded, or too old): ignored; a later one will be
};

/// The client's side: keeps the last kSnapshotHistory snapshots it decoded, each as the humans it leaves held, and
/// decodes a new one against its baseline. Strict, since snapshots come from the network.
/// @coney-only network play; the original has none
class SnapshotReceiver {
  public:
    /// Decodes `snapshot` against its baseline. Fails with Invalid when it breaks what its baseline allows: a
    /// removal of a human the baseline does not hold, a new human with fields left out, or a changed field whose word
    /// is the baseline's.
    [[nodiscard]] std::expected<SnapshotApplied, Error> apply(const Snapshot& snapshot);

    /// The newest sequence decoded; 0 for none.
    [[nodiscard]] std::uint32_t newest() const { return m_newest; }
    /// The server's step the newest snapshot shows; 0 for none.
    [[nodiscard]] std::uint32_t newestStep() const { return m_newestStep; }
    /// The humans the newest snapshot holds.
    [[nodiscard]] const SnapshotHumans& humans() const;
    /// The humans decoded snapshot `sequence` holds, while it is kept; null when it is not.
    [[nodiscard]] const SnapshotHumans* holding(std::uint32_t sequence) const;

  private:
    // One snapshot decoded: its sequence (0 for an empty place) and the humans it holds.
    struct Received {
        std::uint32_t sequence = 0;
        SnapshotHumans holding;
    };

    std::uint32_t m_newest = 0;
    std::uint32_t m_newestStep = 0;
    std::array<Received, kSnapshotHistory> m_received{}; // by sequence % kSnapshotHistory
};

} // namespace coney::net
