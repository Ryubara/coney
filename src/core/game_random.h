// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace coney {

/// The game's own random numbers, as the script binding `random(low, high)` draws them: a fixed table of 1,024 32-bit
/// numbers in the executable, walked by one index that starts at 0 at power-on, is never seeded and is shared by
/// every caller, so a draw depends on how many draws the session has made before it
/// (docs/research/flags.md#player-starts).
///
/// The table is data of the game's executable, so Coney never carries it: setTable() takes it from the player's disc
/// at run time (main reads it from `SLUS_212.15`). **Coney's choice** without the table (no disc, another region's
/// executable, a test): a stand-in generator of its own (xorshift32 from a fixed seed) with the same interface, so
/// the draws stay deterministic but are not the game's.
///
/// **Coney's choice** where the page is open: a draw first advances the index and then reads that entry ("the next
/// entry"), so the session's first draw reads entry 1. Only the script binding draws in Coney; the original's C++
/// callers (the character set-up among them) draw from the same index, so the game makes more draws between two of
/// a script's than Coney does.
///
/// Research: docs/research/flags.md#player-starts, docs/research/scripting.md#open-questions
class GameRandom {
  public:
    /// Entries in the game's table.
    static constexpr std::size_t kTableSize = 1024;
    /// Where the table is in the NTSC-U executable `SLUS_212.15` (a virtual address).
    static constexpr std::uint32_t kTableAddress = 0x005117e0;
    /// The executable that holds it, a file in the disc's root.
    static constexpr const char* kExecutableName = "SLUS_212.15";

    /// Draws from `table`, the game's kTableSize numbers, from now on. A table of another size is a programmer error
    /// (CONEY_ASSERT). The index is kept.
    void setTable(std::span<const std::uint32_t> table);
    /// Whether the game's table is set (else the stand-in draws).
    [[nodiscard]] bool hasTable() const { return !m_table.empty(); }

    /// The next raw number: the index advanced by one and masked to 10 bits, then that entry of the table.
    /// Replaces the original's Random_Next at 0x00335390, inside the Lua middleware range (not counted as progress).
    [[nodiscard]] std::uint32_t next();

    /// A whole number in [low, high]: `low + next() mod (high - low + 1)`, the modulo unsigned as the original's. A
    /// range of 0 numbers (high = low - 1, which the original divides by) gives `low` (Coney's choice).
    /// Replaces the original's Random_Range at 0x003353f0, also inside the Lua middleware range.
    [[nodiscard]] std::int32_t range(std::int32_t low, std::int32_t high);

    /// The index: how many draws, modulo 1,024, the session has made.
    [[nodiscard]] std::uint32_t index() const { return m_index; }

  private:
    std::vector<std::uint32_t> m_table;
    std::uint32_t m_index = 0;
    std::uint32_t m_standIn = 0x12345678U; // the stand-in generator's state
};

} // namespace coney
