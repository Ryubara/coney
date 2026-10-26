// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus lead to Armies of the Night's first stage and not to a
// black screen (owner item 19, issue #85; docs/research/armies.md#coney-fixes). The level table's record `armies` has
// no level file, so the Levels page leaves it out and a jump to it is refused; `level60` checkpoint 1, which the
// Missions page now lists, is jumped to from a level in play and plays its intro scene to the end with the picture
// clear. The session runs as `coney` runs it (support/disc_session.h), the pad idle. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "camera/cameras.h"
#include "camera/rail_camera.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "gamemodes/armies_game_over_mode.h"
#include "gamemodes/start_up_flow.h"
#include "hud/armies_hud.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/game_state.h"

namespace {

// How many of the session's log lines start with `text`.
std::size_t linesStarting(const std::vector<std::string>& lines, std::string_view text) {
    return static_cast<std::size_t>(
        std::ranges::count_if(lines, [text](const std::string& line) { return line.starts_with(text); }));
}

// How many frames of `level60` are watched after the jump: its intro scene ends about 21 s in
// (docs/research/armies.md#first-frames).
constexpr std::uint64_t kWatchFrames = 1200;

// Flag_02, where P1.Start03 walks the player (level60.lua's AddFlag), and the walk's radius.
constexpr float kFlag02X = -94.9F;
constexpr float kFlag02Y = 24.0F;
constexpr float kArrivalRadius = 0.5F;
// Frames allowed for the intro scene and the walk (the original's timer starts about 28 s in), for the 99 s timer, and
// for the game over's countdown (10 steps of 1 s) with a margin.
constexpr std::uint64_t kWalkLimit = 40 * 30;
constexpr std::uint64_t kTimerLimit = 110 * 30;
constexpr std::uint64_t kCountdownFrames = 11 * 30;

} // namespace
