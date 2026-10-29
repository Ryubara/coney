// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/player_frame.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "core/game_timer.h"

namespace coney {

// The players' frame, in the original's order (the header's comment): Pad_Update's last step (the Lua pad handlers of
// the buttons pressed) and StopWatch_Update's callback; the step and the wanted timers are their own ports.
// @orig 0x00144fb0 Pad_Update (unknown)
// @orig 0x004233f8 StopWatch_Update (unknown)
void runPlayerFrame(GameState& state, script::ScriptSystem& scripts, const Pads& pads, std::uint64_t nowTicks,
                    CrimeServices* crimes) {
    PlayerState& player = state.player;
    const std::uint64_t nowMs = nowTicks / (GameTimer::kTicksPerSecond / 1000);

    // Not ported: the pad's vibration from the record's two motor bytes (#179). The left stick's turn by the camera
    // is Pad::update's.
    // The Lua pad handlers of both ports' newly pressed buttons, lowest bit first.
    for (const std::size_t port : {std::size_t{0}, std::size_t{1}}) {
        const std::size_t record = Pads::recordOfPort(port);
        for (const std::string& handler : player.pads.due(record, pads.record(record).pressed())) {
            scripts.call(handler);
        }
    }

    // The stopwatch; its callback once it reaches the target.
    const StopWatch::Step watch = player.stopWatch.step(nowTicks);
    if (watch.finished && !player.stopWatch.callback().empty()) {
        scripts.call(player.stopWatch.callback());
    }

    // The wanted timers, telling the gangs (or no one).
    CrimeServices none;
    player.crimes.update(crimes != nullptr ? *crimes : none, nowMs);
}

// The level's crime state cleared among InitLevel's resets: the wanted timers live in the gangs' records there
// (Gangs_ResetAll), in the players' state in Coney.
// @orig 0x0015fe90 InitLevel (InitLevel.cpp)
void startPlayerLevel(GameState& state) { state.player.crimes.clearLevel(); }

} // namespace coney
