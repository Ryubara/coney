// SPDX-License-Identifier: GPL-3.0-or-later
// The radar's blips as the snapshots carry them (docs/guides/snapshots.md#hud-blips). Stub (the test commit's): each
// function returns a default, so the tests that pin the behaviour fail until the fix commit.
#include "net/radar_blips.h"

#include <cstdint>
#include <map>
#include <vector>

#include "hud/radar.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/snapshot.h"

namespace coney::net {

BlipWords blipWords(const hud::RadarBlip& /*blip*/, NetId /*follows*/) { return {}; }

hud::RadarBlip radarBlipOf(const BlipWords& /*words*/) { return {}; }

std::uint32_t blipPriority(const hud::RadarBlip& /*blip*/) { return 1; }

std::map<double, std::uint8_t> BlipSlots::assign(const std::map<double, hud::RadarBlip>& /*blips*/) { return {}; }

void BlipSlots::clear() {}

std::vector<SnapshotBlip> snapshotBlips(const std::map<double, hud::RadarBlip>& /*blips*/, BlipSlots& /*slots*/,
                                        const BlipFollowed& /*followed*/) {
    return {};
}

std::map<double, hud::RadarBlip> hudBlips(const SnapshotBlips& /*held*/) { return {}; }

} // namespace coney::net
