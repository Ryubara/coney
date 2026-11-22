// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "hud/hud.h"
#include "net/client_world.h"
#include "net/net_id.h"
#include "net/net_id_table.h"
#include "net/player_huds.h"
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
/// airborne), knocked out or wounded, and its move style (hidden while its animator's stealth style is on). A layer the
/// stack does not hold is "no such layer" (clip 0, fade 1). Clip ids over the field's range are held at its top (the
/// field table's stand-in range, replicated_fields.cpp).
/// @coney-only network play; the original has none
[[nodiscard]] HumanState replicatedState(const human::Human& human);

/// The humans of the server's game, each step, as the snapshots and the digest take them. Each human gets a network id
/// the first step it is seen (in brain order, so the same game gives the same ids) and keeps it; a human gone from the
/// brains has its id retired, never handed out again in that generation until releaseRetired() frees it. Ids come from
/// slots never used first, so releasing changes no id until all 65,536 slots have been used. Simulation-side state,
/// owned by whatever writes the game's snapshots or digest.
///
/// Humans are told apart by address: a human removed and another made at the same address within one step would keep
/// the old one's id, and its first snapshot would read as the old human changing. Nothing checks that a level never
/// does this; the human's words are still right, only the id's history is.
/// @coney-only network play; the original has none
class GameHumans {
  public:
    /// This step's humans: every brain's human of `brains`, in slot order, with its id and its words.
    void capture(const ai::Brains& brains);

    /// This step's HUD state of each player slot (slot i at index i, at most kMaxPlayerSlots), as the play mode's HUD
    /// step was given it, and the HUD's standing state after that step (`standing`; null for none): what the snapshots
    /// carry for each player's HUD. Nothing while capture is off.
    void captureHuds(std::span<const hud::PlayerHudState> slots, const hud::HudStanding* standing = nullptr);

    /// No humans and no HUDs: the level has gone. The humans' ids are retired as the next capture() would.
    void clear();
    /// Frees every retired id, for use again in its next generation: call it only when no peer can still name them
    /// (no session, or every client has acknowledged the removals).
    void releaseRetired();
    /// Whether capture() takes the humans (on at first).
    [[nodiscard]] bool capturing() const { return m_capturing; }
    /// Turns capture() and captureHuds() on or off. Turning it off clears the humans and the HUDs and releases every
    /// id, so call it with false only when no peer can still name them; while off, the captures take nothing.
    void setCapturing(bool on);

    /// The humans captured, in brain order, each with priority 1.
    [[nodiscard]] std::span<const SnapshotHuman> humans() const { return m_humans; }
    /// The humans captured by id: what a snapshot that sent them all would leave a client holding.
    [[nodiscard]] SnapshotHumans holding() const;
    /// The players' HUDs captured, in slot order.
    [[nodiscard]] std::span<const SnapshotHud> huds() const { return m_huds; }
    /// The HUD's standing state captured: its elements, in key order.
    [[nodiscard]] std::span<const SnapshotHudElement> hudElements() const { return m_hudStanding.elements; }
    /// The HUD's standing state captured: its texts, in key order.
    [[nodiscard]] std::span<const SnapshotHudText> hudTexts() const { return m_hudStanding.texts; }

  private:
    NetIdAllocator m_ids;
    NetIdTable<const human::Human*> m_table;
    std::vector<SnapshotHuman> m_humans;
    std::vector<SnapshotHud> m_huds;
    SnapshotHudStanding m_hudStanding;
    std::vector<NetId> m_retired; // retired ids waiting for releaseRetired()
    bool m_capturing = true;
};

} // namespace coney::net
