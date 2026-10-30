// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace coney {

/// The original's ten random streams: ten indices into one table, 8 bytes apart from `0x006eb870`, each walked only
/// by its own callers, so a draw on one never moves another (docs/research/maths.md#random). The names say who draws
/// from each in the original (the callers of the stream's index, read in Ghidra).
enum class RandomStream : std::uint8_t {
    Animation = 0, ///< `0x006eb870`: the face clips, attack chain delays and landing clips.
    GangClips,     ///< `0x006eb878`: the gangs' clip sets.
    Gameplay,      ///< `0x006eb880`: brains, goals, a new human's money and size, and the scripts' `random`.
    Cameras,       ///< `0x006eb888`: the cameras' shakes and shot placements.
    Civilians,     ///< `0x006eb890`: civilian reactions and gaits, the flag net's traversal.
    Grabs,         ///< `0x006eb898`: grabs, hit reactions, idle fidgets, a new human's woundable roll.
    Tactics,       ///< `0x006eb8a0`: the gangs' tactics, crew tag lines, wander, banter, spawner placement.
    Ambient,       ///< `0x006eb8a8`: light flicker, fog and smoke, cars, radios, the rumble intro, object groups.
    Sound,         ///< `0x006eb8b0`: sound volumes, impact sounds, ambient emitters, music, a voice pick.
    Effects,       ///< `0x006eb8b8`: particles and world objects (glass, debris, boards, money amounts), strikes.
};

/// How many streams there are.
inline constexpr std::size_t kRandomStreams = 10;

/// Where stream `stream`'s index is in the NTSC-U executable's memory (`0x006eb870` + 8 × its number).
[[nodiscard]] constexpr std::uint32_t randomStreamAddress(RandomStream stream) {
    constexpr std::uint32_t kFirstIndex = 0x006eb870;
    constexpr std::uint32_t kIndexStride = 8;
    return kFirstIndex + (kIndexStride * static_cast<std::uint32_t>(stream));
}

/// The game's own random numbers: a fixed table of 1,024 32-bit numbers in the executable, walked by ten indices, the
/// streams (RandomStream), each starting at 0 at power-on (`Random_StaticInit`, `0x00335608`) and never seeded. Every
/// draw names its stream, so a draw on one (a shard of glass, a camera's shake) never moves another's: the gameplay
/// stream's draws depend only on how many gameplay draws the session has made (docs/research/maths.md#random,
/// docs/research/flags.md#player-starts).
///
/// The table is data of the game's executable, so Coney never carries it: setTable() takes it from the player's disc
/// at run time (main reads it from `SLUS_212.15`). **Coney's choice** without the table (no disc, another region's
/// executable, a test): a stand-in generator of its own per stream (xorshift32 from a fixed seed, the same for every
/// stream as the original's indices all start at 0) with the same interface, so the draws stay deterministic but are
/// not the game's.
///
/// A draw first advances the stream's index and then reads that entry, so a stream's first draw reads entry 1
/// (`Random_NextRaw`, `0x00335390`).
///
/// Research: docs/research/maths.md#random, docs/research/flags.md#player-starts
class GameRandom {
  public:
    /// Entries in the game's table.
    static constexpr std::size_t kTableSize = 1024;
    /// Where the table is in the NTSC-U executable `SLUS_212.15` (a virtual address).
    static constexpr std::uint32_t kTableAddress = 0x005117e0;
    /// The executable that holds it, a file in the disc's root.
    static constexpr const char* kExecutableName = "SLUS_212.15";

    /// Draws from `table`, the game's kTableSize numbers, from now on. A table of another size is a programmer error
    /// (CONEY_ASSERT). The indices are kept.
    void setTable(std::span<const std::uint32_t> table);
    /// Whether the game's table is set (else the stand-in draws).
    [[nodiscard]] bool hasTable() const { return !m_table.empty(); }

    /// The next raw number of `stream`: its index advanced by one and masked to 10 bits, then that entry of the table.
    /// Replaces the original's Random_NextRaw at 0x00335390, inside the Lua middleware range (not counted as progress).
    [[nodiscard]] std::uint32_t next(RandomStream stream);

    /// A whole number in [low, high] from `stream`: `low + next() mod (high - low + 1)`, the modulo unsigned as the
    /// original's; so `range(stream, 0, n)` is the original's `Random_Int(n)`, 0 to n inclusive. A range of 0 numbers
    /// (high = low - 1, which the original divides by) gives `low` (Coney's choice).
    /// Replaces the original's Random_IntRange at 0x003353f0, also inside the Lua middleware range.
    [[nodiscard]] std::int32_t range(RandomStream stream, std::int32_t low, std::int32_t high);

    /// Starts the gameplay stream (RandomStream::Gameplay) at `offset` (masked to 10 bits) as if that many gameplay
    /// draws had been made since power-on: a run's `--ai-seed`, the way each recording of the original starts the index
    /// at `0x006eb880` somewhere else. The other streams keep their places. The stand-in's state moves with it
    /// (**Coney's choice**: its start plus the offset times an odd constant), so a run without the table varies too. 0
    /// is the power-on stream.
    void startAt(std::uint32_t offset);

    /// `stream`'s index: how many draws, modulo 1,024, the session has made from it.
    [[nodiscard]] std::uint32_t index(RandomStream stream) const { return m_index.at(slot(stream)); }

  private:
    // The stream's place in the arrays below.
    static constexpr std::size_t slot(RandomStream stream) { return static_cast<std::size_t>(stream); }

    static constexpr std::uint32_t kStandInStart = 0x12345678U; // the stand-in's power-on state

    std::vector<std::uint32_t> m_table;
    std::array<std::uint32_t, kRandomStreams> m_index{}; // each stream's table index
    std::array<std::uint32_t, kRandomStreams> m_standIn{kStandInStart, kStandInStart, kStandInStart, kStandInStart,
                                                        kStandInStart, kStandInStart, kStandInStart, kStandInStart,
                                                        kStandInStart, kStandInStart}; // each stream's stand-in state
};

} // namespace coney
