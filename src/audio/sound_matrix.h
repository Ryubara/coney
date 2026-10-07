// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coney::audio {

/// One alternative of a sound-matrix entry: up to three sounds (name hashes, 0 for none) and each column's volume.
struct MatrixSounds {
    std::array<std::uint32_t, 3> sounds{};          ///< Columns 1-3; 0 where the column is unused or set to `none`.
    std::array<float, 3> volumes{1.0F, 1.0F, 1.0F}; ///< The volumes of columns 1-3.
};

/// The sound matrix (docs/research/sound.md#sound-matrix): the sounds two materials make when they meet (a fist on a
/// torso, a shoe on concrete, glass on glass) and the sounds of the animation sound events (`SA.*`, clip event 11).
/// The level's `<matrix>_preload.lua` fills it through `NewMaterialSlots` / `NewMaterialSound` / `NewAnimSlots` /
/// `NewAnimSound`; a lookup returns an entry's alternatives **in turn**, not at random.
///
/// Coney keeps each entry's alternatives in its own vectors rather than the original's fixed pools (3,000 hash words,
/// 1,200 material and 150 animation records): the pools only bound how much a preload may configure.
///
/// Research: docs/research/sound.md#sound-matrix
class SoundMatrix {
  public:
    /// The material table's side: materials 0-190 (`MATERIAL.*` of enum_preload.lua).
    static constexpr std::uint32_t kMaterials = 191;
    /// The animation sound events, `SA.*` 0-161.
    static constexpr std::uint32_t kAnimSounds = 162;
    /// The columns an alternative can have.
    static constexpr std::uint32_t kColumns = 3;

    /// An empty matrix named `sound`, the name the game starts with.
    /// @orig 0x00114220 SoundMatrix_Init (unknown)
    SoundMatrix();

    /// The matrix's name (`sound`, `armies`).
    [[nodiscard]] const std::string& name() const { return m_name; }
    /// `SndLoadMatrix(name)`: a name other than the current one empties the matrix and becomes current; returns
    /// whether it did (the caller then runs `<name>_preload.lua`).
    /// @orig 0x00114628 SoundMatrix_Load (unknown)
    bool load(std::string_view name);
    /// Empties both tables (the name stays).
    /// @orig 0x00115c30 SoundMatrix_ClearMaterials (unknown)
    /// @orig 0x00115c80 SoundMatrix_ClearAnims (unknown)
    void clear();

    /// `NewMaterialSlots(m1, m2, count, columns, v1, v2, v3)`: a new entry for the pair with `count` alternatives of
    /// `columns` (1-3) silent columns at those volumes, replacing the pair's entry. Out-of-range materials do nothing.
    /// @orig 0x00115cb0 SoundMatrix_NewMaterialSlots (unknown)
    void newMaterialSlots(std::uint32_t m1, std::uint32_t m2, std::uint32_t count, std::uint32_t columns,
                          const std::array<float, 3>& volumes);
    /// `NewMaterialSound(i, m1, m2, s1, s2, s3)`: alternative `i`'s columns; a sound of nullopt leaves its column, a
    /// hash of 0 (`none`) empties it. Nothing for a pair without an entry or an `i` past its alternatives.
    /// @orig 0x00115ee8 SoundMatrix_NewMaterialSound (unknown)
    void newMaterialSound(std::uint32_t index, std::uint32_t m1, std::uint32_t m2,
                          const std::array<std::optional<std::uint32_t>, 3>& sounds);
    /// `SetNumberOfMaterialSlots(m1, m2, n)`: how many of the pair's alternatives are used (at most those made), and
    /// the choice starts again from the first.
    /// @orig 0x00116480 SoundMatrix_SetMaterialCount (unknown)
    void setMaterialSlotCount(std::uint32_t m1, std::uint32_t m2, std::uint32_t count);
    /// `DuplicateSoundMaterials(a, b)`: the pair (b, a) shares (a, b)'s entry, its turn included; nothing for a == b.
    /// @orig 0x00116438 SoundMatrix_DuplicateMaterials (unknown)
    void duplicateMaterials(std::uint32_t a, std::uint32_t b);
    /// `NewAnimSlots(event, count, columns, v1, v2, v3)`.
    /// @orig 0x00116080 SoundMatrix_NewAnimSlots (unknown)
    void newAnimSlots(std::uint32_t event, std::uint32_t count, std::uint32_t columns,
                      const std::array<float, 3>& volumes);
    /// `NewAnimSound(i, event, s1, s2, s3)`, as newMaterialSound().
    /// @orig 0x001162a0 SoundMatrix_NewAnimSound (unknown)
    void newAnimSound(std::uint32_t index, std::uint32_t event,
                      const std::array<std::optional<std::uint32_t>, 3>& sounds);

    /// The next alternative of the pair (m1, m2), moving the pair's turn on: a second material of 0 or 1 stands for
    /// `fallback` (the caller's default, 5 `CONCRETE` for most), and a pair with no entry falls back to (m1,
    /// `fallback`). Nothing when neither has an entry.
    /// @orig 0x001146c0 SoundMatrix_GetMaterialSounds (unknown)
    [[nodiscard]] std::optional<MatrixSounds> nextMaterialSounds(std::uint32_t m1, std::uint32_t m2,
                                                                 std::uint32_t fallback);
    /// The next alternative of animation sound event `event`, moving its turn on; nothing without an entry.
    /// @orig 0x00114800 SoundMatrix_GetAnimSounds (unknown)
    [[nodiscard]] std::optional<MatrixSounds> nextAnimSounds(std::uint32_t event);

    /// How many material pairs have an entry (a duplicated pair counts twice), for logs and tests.
    [[nodiscard]] std::size_t materialEntries() const;
    /// How many animation events have an entry.
    [[nodiscard]] std::size_t animEntries() const;

  private:
    // An entry: its alternatives (each column's hashes), how many are in use, the turn, the volumes.
    struct Entry {
        std::uint32_t used = 0;
        std::uint32_t turn = 0;
        std::uint32_t columns = 0;
        std::vector<std::array<std::uint32_t, 3>> alternatives;
        std::array<float, 3> volumes{};
    };
    // A new entry with `count` silent alternatives.
    Entry* makeEntry(std::uint32_t count, std::uint32_t columns, const std::array<float, 3>& volumes);
    // The cell of the pair (m1, m2), null when out of range.
    [[nodiscard]] Entry** cell(std::uint32_t m1, std::uint32_t m2);
    // Sets alternative `index` of `entry` (null: nothing).
    static void setSounds(Entry* entry, std::uint32_t index, const std::array<std::optional<std::uint32_t>, 3>& sounds);
    // The entry's alternative in turn, moving the turn on.
    static std::optional<MatrixSounds> next(Entry* entry);

    std::string m_name;
    std::deque<Entry> m_entries; // stable addresses: the cells point into it
    std::vector<Entry*> m_materials;
    std::array<Entry*, kAnimSounds> m_anims{};
};

} // namespace coney::audio
