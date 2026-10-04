// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "animation/anim_clip.h"
#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/wad.h"

// A character's data resource: its animations and the table that maps the game's 722 anim ids to them.
// Format: docs/research/characters.md#files.

namespace coney::characters {

/// The chunk types of a character data resource besides its clips: Character Data and Anim Range List.
inline constexpr std::uint32_t kCharacterDataChunk = 0x08;
inline constexpr std::uint32_t kAnimRangeListChunk = 0x45;

/// Anim ids, and the slot that means "the resource manager's default".
inline constexpr std::size_t kAnimIds = 722;
inline constexpr std::uint32_t kDefaultAnimSlot = 0xFFFFFFFF;

/// Bytes of the Character Data chunk: a placeholder word, a second word, the 722 slots at +0x08, then 16 bytes.
inline constexpr std::size_t kCharacterDataBytes = 2912;
inline constexpr std::size_t kAnimSlotsOffset = 0x08;

/// A character's animations and its anim id table.
class CharacterData {
  public:
    /// The clip for anim `id`; nullptr for an id out of range or a slot that takes the resource manager's default
    /// (Coney has no resource manager yet, so defaults are not resolved).
    /// @orig 0x00175080 CharacterInstance_GetAnim (unknown)
    [[nodiscard]] const anim::AnimClip* animation(std::size_t id) const;

    /// The first clip named `name` (exact spelling, as cut to 30 characters by the tools); nullptr when none is.
    [[nodiscard]] const anim::AnimClip* findClip(std::string_view name) const;

    /// The clips in the order the object stack gave them up: the last one the resource loaded comes first.
    [[nodiscard]] std::span<const std::unique_ptr<anim::AnimClip>> clips() const { return m_clips; }

    /// The slot of each anim id: an index into clips(), or kDefaultAnimSlot.
    [[nodiscard]] const std::array<std::uint32_t, kAnimIds>& slots() const { return m_slots; }

    /// The Anim Range List chunk as stored (not decoded yet, characters.md).
    [[nodiscard]] std::span<const std::byte> rangeList() const { return m_rangeList; }

    /// Builds the table from the Character Data chunk and the objects the clips before it left: each distinct slot
    /// value `n`, in increasing order, takes the next clip popped from the object stack (so the last clip loaded
    /// answers the smallest `n`); every slot holding that `n` then names it. Fails with ErrorCode::Invalid for a chunk
    /// of another size, a slot value above 721, or when the object stack runs out of clips first.
    /// @orig 0x0016e258 CharacterData_OnLoaded (unknown)
    [[nodiscard]] static std::expected<CharacterData, Error>
    resolve(std::span<const std::byte> dataChunk, std::vector<std::byte> rangeList, chunk::ChunkStacks& stacks);

  private:
    std::vector<std::unique_ptr<anim::AnimClip>> m_clips;
    std::array<std::uint32_t, kAnimIds> m_slots{};
    std::vector<std::byte> m_rangeList;
};

/// A character's data on the object stack, as the Character Data handler pushes it.
class CharacterDataObject final : public chunk::LoadedObject {
  public:
    explicit CharacterDataObject(CharacterData data) : m_data(std::move(data)) {}

    [[nodiscard]] std::string_view describe() const override { return "character data"; }

    /// The character data.
    [[nodiscard]] CharacterData& data() { return m_data; }

  private:
    CharacterData m_data;
};

/// The `onLoaded` handler of chunk type 0x08 (Character Data): pops the chunk and the Anim Range List (0x45) before
/// it, resolves the anim table with CharacterData::resolve() and pushes a CharacterDataObject. Fails as the pops and
/// resolve() do.
[[nodiscard]] std::expected<void, Error> onCharacterDataLoaded(chunk::ChunkStacks& stacks, std::uint32_t type);

/// Registers the handlers a character data resource needs in `table`: anim::onAnimDataLoaded() for 0x02 and
/// onCharacterDataLoaded() for 0x08.
void addCharacterDataHandlers(chunk::ChunkHandlerTable& table);

/// Loads the character data resource `entry` through the chunk system with `table` (which needs the handlers of
/// addCharacterDataHandlers()) and takes the character it builds. Fails with ErrorCode::NotFound when the entry
/// builds no character data, and as the load does.
/// @orig 0x0016e8f0 ResourceManager_LoadCharacterData (unknown)
[[nodiscard]] std::expected<CharacterData, Error> loadCharacterData(const io::Wad& wad, const io::WadEntry& entry,
                                                                    const chunk::ChunkHandlerTable& table);

} // namespace coney::characters
