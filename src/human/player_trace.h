// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "human/player.h"

// The player's trace: one CSV line per step with what a feel comparison reads (the human's feet, heading, speeds,
// gait, clip and state; the follow camera's position, look-at point, wanted position, distance, angles, band and
// auto-centre turn), so a scripted run can be compared with the original's per-update trace with Coney alone
// (`--trace FILE`, docs/guides/building.md#tracing). Coney's own tool; the original has none.
// Research: docs/research/feel.md#method

namespace coney::human {

/// The CSV header line, ending in a newline: the names of traceLine()'s columns.
[[nodiscard]] std::string_view traceHeader();

/// One CSV line, ending in a newline, for `player` after step `step` (counted from 1): positions in metres (game
/// axes), angles in degrees, speeds in m/s. Columns: step, x, y, z, heading, speed, vz, gait (0-5), clip, traversal,
/// stamina, sprinting (0 or 1), the camera's x, y, z, its look-at point's x, y, z, its wanted position's x, y, z, the
/// camera's distance from its look-at point, its pitch (positive above it) and yaw (the heading its view faces, as
/// the human's are measured), the band's near edge, the target pitch and the auto-centre turn this step.
[[nodiscard]] std::string traceLine(std::uint64_t step, const Player& player);

} // namespace coney::human
