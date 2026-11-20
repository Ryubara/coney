// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>

#include "core/error.h"
#include "net/bit_stream.h"
#include "net/protocol.h"

// The protocol's pieces shared between its source files (docs/guides/multiplayer.md#protocol): protocol.cpp encodes
// and decodes every message, the Snapshot's body, the largest, is in snapshot_wire.cpp, the EventBatch's in
// event_wire.cpp and the level messages' in level_wire.cpp. Not for use outside them.

namespace coney::net {

/// Reads a 32-bit sequence number written with writeVarUint(); larger is Invalid.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<std::uint32_t, Error> readSequence(BitReader& in);

/// Writes a Snapshot after its message id, as Snapshot's comment lays it out. A Snapshot that breaks its rules is a
/// programmer error, as encode() says.
/// @coney-only network play; the original has none
void writeSnapshotBody(BitWriter& out, const Snapshot& snapshot);

/// Reads a writeSnapshotBody() Snapshot after its message id. Fails as decode() says for a Snapshot: Truncated when
/// short, Invalid when it breaks the rules or a field word is over its range.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<Snapshot, Error> readSnapshotBody(BitReader& in);

/// Writes an EventBatch after its message id, as EventBatch's comment lays it out. A batch that breaks its rules is a
/// programmer error, as encode() says.
/// @coney-only network play; the original has none
void writeEventBatchBody(BitWriter& out, const EventBatch& batch);

/// Reads a writeEventBatchBody() batch after its message id. Fails as decode() says for an EventBatch: Truncated when
/// short, Invalid when it breaks the rules or names an unknown kind.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<EventBatch, Error> readEventBatchBody(BitReader& in);

/// Writes a level message (LoadLevel, LevelLoaded, MissionEnd, MovieDone) with its message id, as its comment lays it
/// out. One that breaks its rules (a load of 0, a checkpoint of 0, a level name empty or over kMaxLevelNameBytes) is
/// a programmer error, as encode() says.
/// @coney-only network play; the original has none
void writeLevelMessage(BitWriter& out, const LoadLevel& message);
/// @copydoc writeLevelMessage(BitWriter&, const LoadLevel&)
void writeLevelMessage(BitWriter& out, const LevelLoaded& message);
/// @copydoc writeLevelMessage(BitWriter&, const LoadLevel&)
void writeLevelMessage(BitWriter& out, const MissionEnd& message);
/// @copydoc writeLevelMessage(BitWriter&, const LoadLevel&)
void writeLevelMessage(BitWriter& out, const MovieDone& message);

/// Reads a LoadLevel after its message id: Truncated when short, Invalid for a load or checkpoint of 0, an empty or
/// too long level name or an unknown reason.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<Message, Error> readLoadLevelBody(BitReader& in);
/// Reads a LevelLoaded after its message id: Truncated when short, Invalid for a load of 0.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<Message, Error> readLevelLoadedBody(BitReader& in);
/// Reads a MissionEnd after its message id: Truncated when short, Invalid for a load of 0 or an unknown outcome.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<Message, Error> readMissionEndBody(BitReader& in);
/// Reads a MovieDone after its message id: Truncated when short.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<Message, Error> readMovieDoneBody(BitReader& in);

} // namespace coney::net
