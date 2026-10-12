// SPDX-License-Identifier: GPL-3.0-or-later
// What the AI fighters page spawns per click: which models and where (docs/guides/debug-menu.md#pages). Coney's own
// tool: nothing in the original corresponds.
#include "debug/fighter_spawn.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::debug {

namespace {

// Fighters in a row, and the gap between neighbours and between rows, in metres.
constexpr int kPerRow = 5;
constexpr float kSpacing = 1.2F;

} // namespace

std::uint32_t SpawnRandom::next() {
    // Marsaglia's xorshift32.
    m_state ^= m_state << 13U;
    m_state ^= m_state >> 17U;
    m_state ^= m_state << 5U;
    return m_state;
}

std::size_t SpawnRandom::below(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(next()) % n; }

std::vector<std::string> pickFighterModels(std::span<const std::string> models, std::size_t choice, int amount,
                                           SpawnRandom& random) {
    const auto count = static_cast<std::size_t>(std::clamp(amount, 0, kMaxFighterAmount));
    std::vector<std::string> picked;
    picked.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (models.empty()) {
            picked.emplace_back();
        } else if (choice >= 1 && choice <= models.size()) {
            picked.push_back(models[choice - 1]);
        } else {
            picked.push_back(models[random.below(models.size())]);
        }
    }
    return picked;
}

std::vector<anim::Vec3> fighterSpots(anim::Vec3 playerFeet, float playerHeadingDegrees, float distance, int amount) {
    const int count = std::clamp(amount, 0, kMaxFighterAmount);
    // Heading 0 faces +y and grows counter-clockwise: forward is (-sin, cos), the player's right (cos, sin).
    const float radians = playerHeadingDegrees * std::numbers::pi_v<float> / 180.0F;
    const float forwardX = -std::sin(radians);
    const float forwardY = std::cos(radians);
    std::vector<anim::Vec3> spots;
    spots.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int row = i / kPerRow;
        const int inRow = std::min(kPerRow, count - row * kPerRow);
        const float across = (static_cast<float>(i % kPerRow) - static_cast<float>(inRow - 1) / 2.0F) * kSpacing;
        const float ahead = distance + static_cast<float>(row) * kSpacing;
        spots.push_back(anim::Vec3{playerFeet.x + forwardX * ahead + std::cos(radians) * across,
                                   playerFeet.y + forwardY * ahead + std::sin(radians) * across, playerFeet.z});
    }
    return spots;
}

} // namespace coney::debug
