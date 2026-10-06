// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "core/pads.h"
#include "scripting/script_system.h"
#include "warriors/crime_reports.h"
#include "warriors/game_state.h"

namespace coney {

/// What a frame of play does to the players' state (GameState::player) before the scripts' frame, in the original's
/// order: the Lua pad handlers of the buttons pressed this step on both ports (`Pad_Update` step 5; the pause mode is
/// not on top while gameplay updates), the mission stopwatch (mode 1 step 1, its callback called when it reaches its
/// target), and the gangs' wanted timers (`Gang_UpdateWanted`, through `crimes`, null: no gangs to tell).
///
/// **Coney's stand-ins:** the Lua pad handlers run only during play, not on the front end; the stopwatch's warning
/// beep plays no sound (open items on docs/research/player-state.md).
///
/// Research: docs/research/scripting.md#stopwatch, docs/research/frontend.md#input, docs/research/crimes.md#wanted
void runPlayerFrame(GameState& state, script::ScriptSystem& scripts, const Pads& pads, std::uint64_t nowMs,
                    CrimeServices* crimes = nullptr);

/// A level starts: its gangs are new, so none is wanted and nothing is at a crime scene yet (the wanted timers live in
/// the gangs in the original).
void startPlayerLevel(GameState& state);

} // namespace coney
