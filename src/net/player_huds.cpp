// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/player_huds.h"

#include <span>
#include <vector>

#include "hud/hud.h"
#include "net/protocol.h"
#include "net/snapshot.h"

namespace coney::net {

HudWords playerHudWords(const hud::PlayerHudState& /*state*/) { return {}; }

hud::PlayerHudState playerHudState(const HudWords& /*words*/) { return {}; }

std::vector<SnapshotHud> snapshotHuds(std::span<const hud::PlayerHudState> /*slots*/) { return {}; }

} // namespace coney::net
