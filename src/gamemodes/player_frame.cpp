// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/player_frame.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace coney {

void runPlayerFrame(GameState& state, script::ScriptSystem& scripts, const Pads& pads, std::uint64_t nowMs,
                    CrimeServices* crimes) {
    PlayerState& player = state.player;

    // The Lua pad handlers of both ports' newly pressed buttons, lowest bit first.
    for (const std::size_t port : {std::size_t{0}, std::size_t{1}}) {
        const std::size_t record = Pads::recordOfPort(port);
        for (const std::string& handler : player.pads.due(record, pads.record(record).pressed())) {
            scripts.call(handler);
        }
    }

    // The stopwatch; its callback once it reaches the target.
    const StopWatch::Step watch = player.stopWatch.step(nowMs);
    if (watch.finished && !player.stopWatch.callback().empty()) {
        scripts.call(player.stopWatch.callback());
    }

    // The wanted timers, telling the gangs (or no one).
    CrimeServices none;
    player.crimes.update(crimes != nullptr ? *crimes : none, nowMs);
}

void startPlayerLevel(GameState& state) { state.player.crimes.clearLevel(); }

} // namespace coney
