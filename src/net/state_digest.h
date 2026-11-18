// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/error.h"
#include "core/pads.h"
#include "net/net_id.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"

// The state digest: a hash of the replicated state at a step, the same on every machine, which a run writes one line a
// step of (`--digest FILE`) and the network tests compare: a guest's decoded snapshots against what the host's
// snapshots left it holding, an offline run against the same run with networking in it, a replay against the session
// it recorded. Coney's own; the original has no network play (docs/guides/multiplayer.md#digest).

namespace coney::net {

/// A 64-bit FNV-1a hash fed whole numbers a byte at a time, least significant first, so the value does not depend on
/// the machine's byte order. Fast, and enough to tell two states apart; nothing here needs a cryptographic hash.
/// @coney-only network play; the original has none
class DigestHasher {
  public:
    /// Feeds the low `bytes` bytes of `value` (1 to 8; CONEY_ASSERT).
    void add(std::uint64_t value, std::size_t bytes);
    /// The hash of everything fed so far.
    [[nodiscard]] std::uint64_t value() const { return m_hash; }

  private:
    std::uint64_t m_hash = 0xcbf29ce484222325ULL; // FNV-1a's 64-bit offset basis
};

/// The digest of the humans a snapshot holds (or a server's game hands its snapshots): their count, then each one in id
/// order, its id and its field words. Words, not values, so a client's decoded state and the server's compare exactly.
/// @coney-only network play; the original has none
[[nodiscard]] std::uint64_t humansDigest(const SnapshotHumans& humans);

/// The digest of the pads a step played, port 1 then port 2: whether each is plugged in and, only when it is, its
/// buttons, sticks and pressures (an unplugged pad's other bytes mean nothing to the game).
/// @coney-only network play; the original has none
[[nodiscard]] std::uint64_t padsDigest(const PortSamples& pads);

/// Where two holdings of humans first differ, in id order.
/// @coney-only network play; the original has none
struct HumanDifference {
    NetId id; ///< the first human that differs
    /// Its first field that differs; nothing when only one of the two holds the human.
    std::optional<HumanField> field;

    friend bool operator==(const HumanDifference&, const HumanDifference&) = default;
};

/// The first human and field in which `first` and `second` differ; nothing when they are the same. The debug dump
/// that says what two different digests are about.
/// @coney-only network play; the original has none
[[nodiscard]] std::optional<HumanDifference> firstDifference(const SnapshotHumans& first, const SnapshotHumans& second);

/// `difference` (from firstDifference(first, second)) for a log: "human SLOT.GEN FIELD: WORD against WORD", or
/// "human SLOT.GEN: held by the first only" (or the second).
/// @coney-only network play; the original has none
[[nodiscard]] std::string describeDifference(const HumanDifference& difference, const SnapshotHumans& first,
                                             const SnapshotHumans& second);

/// One line of a digest file: a step and its digests.
/// @coney-only network play; the original has none
struct DigestLine {
    std::uint32_t step = 0;       ///< the server's step the state is at
    std::uint64_t humans = 0;     ///< humansDigest()
    std::uint32_t humanCount = 0; ///< how many humans it holds, for a reader
    std::optional<std::uint64_t>
        pads; ///< padsDigest() of the step's pads, where the writer played them; a guest has none

    friend bool operator==(const DigestLine&, const DigestLine&) = default;
};

/// `digest STEP HUMANS COUNT [PADS]`: the step and count in decimal, the digests as 16 lowercase hex digits.
/// @coney-only network play; the original has none
[[nodiscard]] std::string formatDigestLine(const DigestLine& line);

/// Reads a formatDigestLine() line, strictly: Invalid for anything else (another word, a digest not of 16 lowercase hex
/// digits, a missing or extra field, a number out of range).
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<DigestLine, Error> parseDigestLine(std::string_view text);

/// What matchDigests() found.
/// @coney-only network play; the original has none
struct DigestMatch {
    std::size_t matched = 0;                    ///< the guest's lines whose step the host wrote, with the same digest
    std::optional<std::uint32_t> firstMismatch; ///< the first step both wrote with another digest
    std::optional<std::uint32_t> firstUnknown;  ///< the first step the guest wrote and the host did not
};

/// Compares a guest's lines with the host's by step: each line the guest wrote must have a host line of its step with
/// the same humans' digest and count. The guest may skip steps (a lost or stale snapshot), so only its lines are
/// walked. Pads are not compared: a guest has none.
/// @coney-only network play; the original has none
[[nodiscard]] DigestMatch matchDigests(std::span<const DigestLine> host, std::span<const DigestLine> guest);

} // namespace coney::net
