// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/player_stats.h"

#include <algorithm>

namespace coney {

PlayerStats::PlayerStats() : m_points(zeroed()), m_counts{zeroed(), zeroed()} {}

PlayerStats::Counters PlayerStats::zeroed() {
    Counters counters;
    for (std::size_t category = 0; category < kCategories; ++category) {
        counters.at(category).assign(kEvents.at(category), 0);
    }
    return counters;
}

bool PlayerStats::valid(std::size_t category, std::size_t index) {
    return category < kCategories && index < kEvents.at(category);
}

void PlayerStats::setPoints(std::size_t category, std::size_t index, std::uint16_t points) {
    if (valid(category, index)) {
        m_points.at(category).at(index) = points;
    }
}

std::uint16_t PlayerStats::points(std::size_t category, std::size_t index) const {
    return valid(category, index) ? m_points.at(category).at(index) : 0;
}

void PlayerStats::setMaximum(StatCategory category, std::uint32_t maximum) {
    m_maxima.at(static_cast<std::size_t>(category)) = maximum;
}

std::uint32_t PlayerStats::maximum(StatCategory category) const {
    return m_maxima.at(static_cast<std::size_t>(category));
}

void PlayerStats::add(int player, std::size_t category, std::size_t index, std::uint32_t amount) {
    if (player < 0 || player >= kPlayers || !valid(category, index)) {
        return;
    }
    std::uint16_t& counter = m_counts.at(static_cast<std::size_t>(player)).at(category).at(index);
    counter = static_cast<std::uint16_t>(counter + amount);
}

std::uint16_t PlayerStats::count(int player, std::size_t category, std::size_t index) const {
    if (player < 0 || player >= kPlayers || !valid(category, index)) {
        return 0;
    }
    return m_counts.at(static_cast<std::size_t>(player)).at(category).at(index);
}

std::uint32_t PlayerStats::categoryPoints(int player, StatCategory category) const {
    if (player < 0 || player >= kPlayers) {
        return 0;
    }
    const auto group = static_cast<std::size_t>(category);
    const std::vector<std::uint16_t>& counts = m_counts.at(static_cast<std::size_t>(player)).at(group);
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        sum += static_cast<std::uint32_t>(counts.at(index)) * m_points.at(group).at(index);
    }
    return sum;
}

std::uint32_t PlayerStats::categoryScore(int player, StatCategory category) const {
    const std::uint32_t points = categoryPoints(player, category);
    if (category != StatCategory::Harmony) {
        return points;
    }
    const std::uint32_t most = maximum(category);
    return points >= most ? 0 : most - points;
}

std::uint32_t PlayerStats::categoryPercent(int player, StatCategory category) const {
    const std::uint32_t most = maximum(category);
    if (most == 0) {
        return 0;
    }
    const auto percent = static_cast<std::uint64_t>(categoryScore(player, category)) * 100 / most;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(percent, 100));
}

std::uint32_t PlayerStats::score(int player) const {
    if (player < 0 || player >= kPlayers) {
        return 0;
    }
    // Coney's stand-in (the header): the five scoring categories less the harmony penalties.
    std::uint32_t gained = 0;
    for (const StatCategory category :
         {StatCategory::Mission, StatCategory::Bonus, StatCategory::Style, StatCategory::Combat, StatCategory::Crime}) {
        gained += categoryPoints(player, category);
    }
    const std::uint32_t lost = categoryPoints(player, StatCategory::Harmony);
    return gained >= lost ? gained - lost : 0;
}

void PlayerStats::resetPlayer(int player) {
    if (player >= 0 && player < kPlayers) {
        m_counts.at(static_cast<std::size_t>(player)) = zeroed();
    }
}

void PlayerStats::reset() {
    for (int player = 0; player < kPlayers; ++player) {
        resetPlayer(player);
    }
}

} // namespace coney
