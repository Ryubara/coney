// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/client_event_player.h"

namespace coney {

// @coney-only presents the host's events on a guest (network play)
void ClientEventPlayer::apply(const GameEvent& /*event*/) {}

} // namespace coney
