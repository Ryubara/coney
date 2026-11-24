// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// A human's character as a snapshot names it: an id, never a pointer, that the client turns into the character's model
// and clips from its own disc. Coney's own; the original has no network play (docs/guides/snapshots.md#character).

namespace coney::net {

/// The id of "no character named": the human's field is 0 (a human the host could not tell, or a test's human). A
/// client draws such a human as its fallback character.
/// @coney-only network play; the original has none
inline constexpr std::int32_t kNoCharacter = 0;

/// The most a human's character field holds: 10 bits, a Character List index plus one. The disc's Character List has
/// 543 records (docs/research/characters-appearance.md#files), so the width is fixed by the original's own data, with
/// room to spare.
/// @coney-only network play; the original has none
inline constexpr std::int32_t kMostCharacterId = 1023;

/// The id of the character at `listIndex` in the Character List: the index the original keeps in a human at `+0x37c`
/// (docs/research/characters.md, "model index in the Character List"), plus one so that 0 is "none". kNoCharacter for
/// an index past kMostCharacterId.
/// @coney-only network play; the original has none
[[nodiscard]] constexpr std::int32_t characterIdOf(std::size_t listIndex) {
    return listIndex < static_cast<std::size_t>(kMostCharacterId) ? static_cast<std::int32_t>(listIndex) + 1
                                                                  : kNoCharacter;
}

/// The Character List index a human's character `id` names; nothing for kNoCharacter or an id past kMostCharacterId.
/// @coney-only network play; the original has none
[[nodiscard]] constexpr std::optional<std::size_t> characterListIndexOf(std::int32_t id) {
    if (id < 1 || id > kMostCharacterId) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(id) - 1;
}

} // namespace coney::net
