// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "core/engine_limits.h"

// Coney's crowd rules: what the engine does past the original's fixed limits, which were sized for its 60 human slots.
// Up to the original's numbers every rule here gives the original's value, so a level of 60 humans or fewer plays as
// on the PS2; past them each rule is Coney's own. Each lifted limit is read through one function of this file, so a
// test preset that pins the engine at the original's numbers has one place per limit.
// Research: docs/engine/crowd-rules.md, docs/guides/conventions.md#original-limits

namespace coney::crowd {

/// The most humans the original holds: its 60 human slots (`0x00640c80`). Its range ends here.
inline constexpr std::size_t kOrigMaxHumans = 60;
/// The most enemy scans the original runs in one update across all brains (`AI_TakeScanToken` `0x00293cd0`).
inline constexpr int kOrigScanTokens = 5;
/// The most enemy scans Coney runs in one update past the original's range, however large the crowd.
inline constexpr int kCrowdScanTokenCap = 20;

/// The enemy-scan tokens of one update for `scanningBrains` brains that may scan (the gangs' members).
/// @coney-only limit: the original gives 5 tokens an update for its 60 humans; past 60 brains Coney gives 5 per 60
/// (rounded up), at most kCrowdScanTokenCap, and a brain refused a token still asks again on the next update
[[nodiscard]] int scanTokens(std::size_t scanningBrains, EngineLimits limits);

/// The most humans one list of humans round a human holds, among `humans` humans: the steering list, the help call's
/// hearers, a trigger box's or sphere's occupants and the humans a sphere finds.
/// @coney-only limit: the original's lists hold 60 handles, one per human slot; past 60 humans Coney's hold every
/// human, so no list is cut in table order
[[nodiscard]] std::size_t humanListCap(std::size_t humans, EngineLimits limits);

} // namespace coney::crowd
