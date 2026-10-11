// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstddef>
#include <utility>

#include "warriors/tag_game.h"

// A player's left stick in the tagging stick game (docs/research/crimes.md#tagging): it steers the cursor along the
// path ahead of the progress, as a player tracing the drawn guide does, so a test follows any path shape (a pattern's
// curve can hook back at its end).

namespace coney::test {

/// The stick (x, y), at `deflection` (0-1), that steers `game`'s cursor toward the first path point after the
/// progress at least 6 cells from it (the progress only moves once the cursor leaves its 6-cell circle); past the
/// path's end, 6 cells on along its last step.
inline std::pair<float, float> tagStick(const TagGame& game, float deflection = 0.8F) {
    const auto& path = game.path();
    if (path.empty()) {
        return {0.0F, 0.0F};
    }
    constexpr float kAhead = 6.0F;
    const std::size_t p = game.progress();
    const auto distance = [](TagCell a, float x, float y) {
        return std::hypot(static_cast<float>(a.x) - x, static_cast<float>(a.y) - y);
    };
    float tx = static_cast<float>(path.back().x);
    float ty = static_cast<float>(path.back().y);
    bool found = false;
    for (std::size_t i = p + 1; i < path.size() && !found; ++i) {
        if (distance(path[i], static_cast<float>(path[p].x), static_cast<float>(path[p].y)) >= kAhead) {
            tx = static_cast<float>(path[i].x);
            ty = static_cast<float>(path[i].y);
            found = true;
        }
    }
    if (!found && path.size() >= 2) {
        const TagCell before = path[path.size() - 2];
        const float step = distance(before, tx, ty);
        if (step > 0.0F) {
            tx += kAhead * (tx - static_cast<float>(before.x)) / step;
            ty += kAhead * (ty - static_cast<float>(before.y)) / step;
        }
    }
    // Aim at the cell's middle: the cursor's cell is its position truncated.
    const float dx = tx + 0.5F - game.cursorX();
    const float dy = ty + 0.5F - game.cursorY();
    const float length = std::hypot(dx, dy);
    if (length < 0.25F) {
        return {0.0F, 0.0F};
    }
    return {deflection * dx / length, deflection * dy / length};
}

} // namespace coney::test
