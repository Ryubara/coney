// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "hud/hud.h"
#include "net/protocol.h"
#include "net/snapshot.h"

// Each player slot's HUD state as the snapshots carry it: hud::PlayerHudState to its field table's words and back.
// Coney's own; the original has no network play (docs/guides/snapshots.md#hud-state).

namespace coney::net {

/// `state`'s fields as words (fieldTable(ReplicatedKind::PlayerHud)). A number beyond its field's range is held at the
/// range's end (**Coney's choice**: the panel clamps the money and the counts tighter still, so what it shows is the
/// same); the panel's `promptWakes`, which the HUD works out from the prompt itself, does not travel.
/// @coney-only network play; the original has none
[[nodiscard]] HudWords playerHudWords(const hud::PlayerHudState& state);

/// The state a player's HUD words stand for: what a client's HUD is given for that slot. `promptWakes` is false.
/// @coney-only network play; the original has none
[[nodiscard]] hud::PlayerHudState playerHudState(const HudWords& words);

/// The HUDs of `slots` (slot i at index i, at most kMaxPlayerSlots) as a snapshot takes them, in slot order.
/// @coney-only network play; the original has none
[[nodiscard]] std::vector<SnapshotHud> snapshotHuds(std::span<const hud::PlayerHudState> slots);

} // namespace coney::net
