// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace coney {

/// The six statistic categories, `StatAdd`'s groups and `CfgSetStatValue`'s tables (docs/references/statistics.md).
enum class StatCategory : std::uint8_t { Mission = 0, Bonus = 1, Style = 2, Combat = 3, Crime = 4, Harmony = 5 };

/// The statistics object (`0x006fe490`): per player, a 16-bit counter per event in six categories, each event worth
/// the points of its category's table (`CfgSetStatValue`), and each category's maximum (`CfgSetStatTypeMax`). A
/// category's score is the sum of count × points (`0x004211d8`); harmony's is its maximum minus that sum
/// (`0x00422a90`), so harmony events are penalties; the end-of-mission screen shows a score as a percentage of the
/// maximum, at most 100 (`0x00422b00`).
///
/// **Coney's stand-in** for the player's score (`StatGetScore`, computed by `0x00422998`, whose formula is not traced):
/// the points of the five other categories less harmony's points, never below 0 (open item on
/// docs/research/player-state.md).
///
/// Players are 0 and 1. An event index past its category's table is ignored (Coney's choice: the original does not
/// check it).
///
/// Research: docs/research/player-state.md#statistics
class PlayerStats {
  public:
    /// Players.
    static constexpr int kPlayers = 2;
    /// Categories.
    static constexpr std::size_t kCategories = 6;
    /// Events per category: the sizes of the points tables at `0x005971b8`, `0x005971c0`, `0x00715510`,
    /// `0x00715500`, `0x00715818` and `0x00715830`.
    static constexpr std::array<std::size_t, kCategories> kEvents{3, 4, 13, 8, 12, 5};

    PlayerStats();

    /// `CfgSetStatValue(table, index, value)`: event `index` of category `category` is worth `points`. Ignored for a
    /// category above 5 or an index past the table.
    /// @orig 0x00422430 Cfg_SetStatValue (unknown)
    void setPoints(std::size_t category, std::size_t index, std::uint16_t points);
    /// The points of one event; 0 outside the tables.
    [[nodiscard]] std::uint16_t points(std::size_t category, std::size_t index) const;
    /// `CfgSetStatTypeMax(combat, crime, harmony, style, mission, bonus)`: the maxima, given here by category.
    void setMaximum(StatCategory category, std::uint32_t maximum);
    /// A category's maximum.
    [[nodiscard]] std::uint32_t maximum(StatCategory category) const;

    /// `StatAdd(human, group, stat, amount)`: adds `amount` to player `player`'s event `index` of `category` (the
    /// counter wraps at 16 bits). Ignored for a bad player, category or index.
    /// @orig 0x004224d8 StatAdd (unknown)
    void add(int player, std::size_t category, std::size_t index, std::uint32_t amount = 1);
    /// One event's count; 0 outside the ranges.
    [[nodiscard]] std::uint16_t count(int player, std::size_t category, std::size_t index) const;

    /// A category's points: the sum of each event's count × its points.
    /// @orig 0x004211d8 Stats_CategoryPoints (unknown)
    [[nodiscard]] std::uint32_t categoryPoints(int player, StatCategory category) const;
    /// A category's score: its points, or for harmony the maximum less the points (not below 0).
    /// @orig 0x00422a90 Stats_CategoryScore (unknown)
    [[nodiscard]] std::uint32_t categoryScore(int player, StatCategory category) const;
    /// A category's score as a percentage of its maximum, at most 100; 0 when the maximum is 0.
    /// @orig 0x00422b00 Stats_CategoryPercent (unknown)
    [[nodiscard]] std::uint32_t categoryPercent(int player, StatCategory category) const;
    /// The player's score (the stand-in above); 0 for a bad player.
    /// @orig 0x00422630 StatGetScore (unknown)
    [[nodiscard]] std::uint32_t score(int player) const;

    /// `StatResetPlayer`: clears one player's counters.
    /// @orig 0x00422718 StatResetPlayer (unknown)
    void resetPlayer(int player);
    /// `StatReset`: clears every player's counters.
    /// @orig 0x004226f8 StatReset (unknown)
    void reset();

  private:
    // One player's counters, by category, each as long as kEvents says.
    using Counters = std::array<std::vector<std::uint16_t>, kCategories>;

    // Whether `category` and `index` name an event.
    [[nodiscard]] static bool valid(std::size_t category, std::size_t index);
    // A fresh set of zeroed counters.
    [[nodiscard]] static Counters zeroed();

    std::array<std::vector<std::uint16_t>, kCategories> m_points;
    std::array<std::uint32_t, kCategories> m_maxima{};
    std::array<Counters, kPlayers> m_counts;
};

} // namespace coney
