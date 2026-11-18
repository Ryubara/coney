// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "net/client_world.h"
#include "net/net_id.h"
#include "net/net_id_table.h"
#include "net/snapshot.h"

// The server's game as snapshots and the state digest see it: each human's replicated state read from the game, and
// the network ids the humans get as they appear and lose as they go. Coney's own; the original has no network play
// (docs/guides/multiplayer.md#digest).

namespace coney::ai {
class Brains;
} // namespace coney::ai

namespace coney::human {
class Human;
} // namespace coney::human

namespace coney::net {

/// `human`'s replicated state now: its feet and heading, health and maximum, its animation stack's layers (newest
/// first, each one's clip, playhead and gait value, and how far the layer over it has faded in), on the ground (not
/// airborne), and knocked out or wounded. A layer the stack does not hold is "no such layer" (clip 0, fade 1). Clip ids
/// over the field's range are held at its top (the field table's stand-in range, replicated_fields.cpp).
/// @coney-only network play; the original has none
[[nodiscard]] HumanState replicatedState(const human::Human& human);

/// The humans of the server's game, each step, as the snapshots and the digest take them. Each human gets a network id
/// the first step it is seen (in brain order, so the same game gives the same ids) and keeps it; a human gone from the
/// brains has its id retired, never handed out again in that generation. Simulation-side state, owned by whatever
/// writes the game's snapshots or digest.
///
/// Humans are told apart by address: a human removed and another made at the same address within one step would keep
/// the id. A level removes and makes humans on different steps, so this does not happen in play.
/// @coney-only network play; the original has none
class GameHumans {
  public:
    /// This step's humans: every brain's human of `brains`, in slot order, with its id and its words.
    void capture(const ai::Brains& brains);

    /// No humans: the level has gone. Their ids are retired as the next capture() would.
    void clear();

    /// The humans captured, in brain order, each with priority 1.
    [[nodiscard]] std::span<const SnapshotHuman> humans() const { return m_humans; }
    /// The humans captured by id: what a snapshot that sent them all would leave a client holding.
    [[nodiscard]] SnapshotHumans holding() const;

  private:
    NetIdAllocator m_ids;
    NetIdTable<const human::Human*> m_table;
    std::vector<SnapshotHuman> m_humans;
};

} // namespace coney::net
