// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "hud/radar.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/snapshot.h"

// The radar's blips as the snapshots carry them: hud::RadarBlip to its field table's words and back, the slot each
// blip takes, and the priority each has. Coney's own; the original has no network play
// (docs/guides/snapshots.md#hud-blips).

namespace coney::net {

/// `blip`'s fields as words (fieldTable(ReplicatedKind::HudBlip)). A blip names its place one of two ways: `follows`,
/// the human it follows, when that is valid(), else its place (`fixedPlace`, or where the last radar step found it)
/// when it has one, else none, with the place's words at the zero place's so one blip has one set of words. A number
/// beyond its field's range is held at the range's end; the blink's and the mark's running counts travel as whether
/// they run, since each HUD counts them down on its own.
/// @coney-only network play; the original has none
[[nodiscard]] BlipWords blipWords(const hud::RadarBlip& blip, NetId follows = {});

/// The blip a client's HUD is given for `words`: the carried fields, `follows` as the id's number, `fixedPlace` the
/// quantized place (none without one), and the running counts at their start (kRadarObjectiveFlash, kRadarMarkUpdates)
/// when they run. Its settled state, its batch's depth and its dot are the client's HUD's own.
/// @coney-only network play; the original has none
[[nodiscard]] hud::RadarBlip radarBlipOf(const BlipWords& words);

/// How fast a blip's priority grows each snapshot its change waits: 4 for a mission objective (types 10 and 1), which
/// the player is steering by, 1 for the rest.
/// @coney-only network play; the original has none
[[nodiscard]] std::uint32_t blipPriority(const hud::RadarBlip& blip);

/// The slot each blip of a level takes in the snapshots: its own for as long as the blip lasts, so a delta names it
/// the same each step. A blip new takes the lowest slot free, in handle order when several are new in one step; a blip
/// gone frees its slot. Blips past kMaxRadarBlips at once have none until one is free.
/// @coney-only network play; the original has none
class BlipSlots {
  public:
    /// The slots after this step's blips: each blip of `blips` that has one, by its handle.
    [[nodiscard]] std::map<double, std::uint8_t> assign(const std::map<double, hud::RadarBlip>& blips);
    /// Every slot free again: the level has gone.
    void clear();
    /// Slots in use.
    [[nodiscard]] std::size_t size() const { return m_slots.size(); }

  private:
    std::map<double, std::uint8_t> m_slots;
    std::bitset<kMaxRadarBlips> m_used;
};

/// The id of the human a blip's script handle names, for the blips that follow one: not valid() for a handle that names
/// no human the session holds an id for.
using BlipFollowed = std::function<NetId(double handle)>;

/// `blips` (the HUD's, by handle) as a snapshot takes them, in slot order: the slots `slots` gives, each blip's words
/// with the human it follows (`followed`; none given: every blip names its place), and its priority.
/// @coney-only network play; the original has none
[[nodiscard]] std::vector<SnapshotBlip> snapshotBlips(const std::map<double, hud::RadarBlip>& blips, BlipSlots& slots,
                                                      const BlipFollowed& followed = {});

/// The blips `held` (what a client holds) stand for, keyed by slot as a number: what a client's HUD takes
/// (hud::SharedHudStanding::blips).
/// @coney-only network play; the original has none
[[nodiscard]] std::map<double, hud::RadarBlip> hudBlips(const SnapshotBlips& held);

} // namespace coney::net
