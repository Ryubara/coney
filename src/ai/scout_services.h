// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>

#include "animation/anim_math.h"

// What the scouts' call for help (TacticScout, ai/tactic_scout.h) asks of the level, apart from the tactic so the
// level's story host can own it.
// Research: docs/research/stealth.md#spotted

namespace coney::ai {

/// The phone flags' activity.
inline constexpr int kPhoneActivity = 6;

/// What the scouts' call asks of the level. Each hook may be empty: then it answers no (or does nothing).
struct ScoutServices {
    /// Whether a responder spawner exists to answer a call (a spawner in state 9 or 10).
    std::function<bool()> responderReady;
    /// The phone a caller at `from` runs to: the farthest phone flag (activity 6) within `radius`; none when there is
    /// none.
    std::function<std::optional<anim::Vec3>(anim::Vec3 from, float radius)> phone;
    /// The call is made: `count` responders after `delaySeconds` toward `point` (`Responders_QueueGangCall`).
    std::function<void(int count, int delaySeconds, anim::Vec3 point)> queueGangCall;
    /// Whether gang `gang`'s second wanted timer runs (gang `+0x5f0`).
    std::function<bool(int gang)> secondWanted;
    /// Starts gang `gang`'s second wanted timer (`Gang_SetSecondWantedTimer`, 10 s).
    std::function<void(int gang)> setSecondWanted;
    /// One caller at a time across the level (game state `+0x284`).
    bool callerActive = false;
};

} // namespace coney::ai
