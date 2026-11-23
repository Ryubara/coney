// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "hud/hud.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/snapshot.h"

// Each player slot's HUD state as the snapshots carry it: hud::PlayerHudState to its field table's words and back.
// Coney's own; the original has no network play (docs/guides/snapshots.md#hud-state).

namespace coney::net {

/// `state`'s fields as words (fieldTable(ReplicatedKind::PlayerHud)), with `human` the human the slot drives (none for
/// a slot that drives none). A number beyond its field's range is held at the range's end (**Coney's choice**: the
/// panel clamps the money and the counts tighter still, so what it shows is the same); the panel's `promptWakes`,
/// which the HUD works out from the prompt itself, does not travel.
/// @coney-only network play; the original has none
[[nodiscard]] HudWords playerHudWords(const hud::PlayerHudState& state, NetId human = {});

/// The human a player slot's HUD words say it drives: none when it drives no human.
/// @coney-only network play; the original has none
[[nodiscard]] NetId playerHudHuman(const HudWords& words);

/// The state a player's HUD words stand for: what a client's HUD is given for that slot. `promptWakes` is false.
/// @coney-only network play; the original has none
[[nodiscard]] hud::PlayerHudState playerHudState(const HudWords& words);

/// The HUDs of `slots` (slot i at index i, at most kMaxPlayerSlots) as a snapshot takes them, in slot order, each
/// naming the human the slot drives: `humans[i]` (slot i drives none when `humans` has no i, or its id is not valid()).
/// @coney-only network play; the original has none
[[nodiscard]] std::vector<SnapshotHud> snapshotHuds(std::span<const hud::PlayerHudState> slots,
                                                    std::span<const NetId> humans = {});

/// The HUD's standing state as a snapshot takes it: its elements and its texts, each in key order.
/// @coney-only network play; the original has none
struct SnapshotHudStanding {
    std::vector<SnapshotHudElement> elements;
    std::vector<SnapshotHudText> texts;
};

/// `standing` as elements and texts (docs/guides/snapshots.md#hud-standing): each player's element; a counter panel
/// taken, a bar or the gauge that exists, a scoreboard row shown and the stopwatch shown, each as its element; the
/// settings always. Each text that is not empty, of an element that is there, cut to kMaxHudTextBytes. A number
/// beyond its field's range is held at the range's end.
/// @coney-only network play; the original has none
[[nodiscard]] SnapshotHudStanding snapshotHudStanding(const hud::HudStanding& standing);

/// The standing state that `elements` and `texts` (what a client holds) stand for: an element not held is as a HUD
/// with no such part has it (a panel released, a bar removed, a row cleared, the stopwatch hidden); the settings, when
/// not held, are the HUD's defaults. Player elements past hud::kPlayers are left out. The cycles' running parts are at
/// their start, and the counter panels have no hide time.
/// @coney-only network play; the original has none
[[nodiscard]] hud::HudStanding hudStanding(const SnapshotHudElements& elements, const SnapshotHudTexts& texts);

/// The HUD a guest in player slot `slot` draws, from the host's: the guest has one player, so his slot's standing state
/// is the one HUD player's (player 0's, the panel and radar that draw with one player) and every other player's is
/// the HUD's defaults; the state shared by all players is the host's as it is. A slot beyond the HUD's players (a
/// third or fourth remote player: the standing state carries two) has the defaults for his player.
/// @coney-only network play; the original has none
[[nodiscard]] hud::HudStanding standingForSlot(const hud::HudStanding& host, std::size_t slot);

/// The state the guest in player slot `slot` is given for his HUD's player 0, from the snapshot's HUDs: what the host
/// had for that slot, or an empty state when the snapshot holds none for it (a slot not playing in the level).
/// @coney-only network play; the original has none
[[nodiscard]] hud::PlayerHudState playerHudForSlot(const SnapshotHuds& huds, std::size_t slot);

} // namespace coney::net
