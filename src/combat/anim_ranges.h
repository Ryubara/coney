// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "core/error.h"

// A character's Anim Range List decoded: per anim id, the direction towards the other human, the reach and far
// ranges, the damage and the hit kind. The character data keeps the chunk as stored
// (characters::CharacterData::rangeList()); combat decodes it here.
// Research: docs/research/formats/animation.md#anim-range-list, docs/research/combat.md#damage-table

namespace coney::combat {

/// One anim id's record (16 bytes in the file).
struct AnimRange {
    float directionX = 0.0F; ///< `+0x00` × 0.001: right of the attacker.
    float directionY = 0.0F; ///< `+0x02` × 0.001: ahead of the attacker.
    float reach = 0.0F;      ///< `+0x04`; 0 means the id has no range data.
    float far = 0.0F;        ///< `+0x08` × 0.001 as stored; 0 when the file leaves it to the reach (farRange()).
    std::int16_t damage = 0; ///< `+0x0a`: the move's damage.
    std::int16_t kind = 0;   ///< `+0x0c`: the hit code (direction, height, strength; combat/reactions.h).
    std::uint16_t flags = 0; ///< `+0x0e`: the playback-rate flags and the stun flag `0x400`.
};

/// The Anim Range List of one character.
class AnimRangeList {
  public:
    /// Bytes of one record.
    static constexpr std::size_t kRecordBytes = 16;

    /// Decodes the chunk: a count word, then `count` records. Fails with ErrorCode::Truncated when the chunk is shorter
    /// than its count says (or has no count word).
    [[nodiscard]] static std::expected<AnimRangeList, Error> parse(std::span<const std::byte> chunk);

    /// How many anim ids have a record.
    [[nodiscard]] std::size_t size() const { return m_records.size(); }
    /// The record of anim `id`, or null when the list is shorter.
    [[nodiscard]] const AnimRange* find(std::size_t id) const;

    /// Anim `id`'s damage (`+0x0a`); 0 for an id without a record.
    /// @orig 0x002542e8 AnimRange_Damage (unknown)
    [[nodiscard]] int damage(std::size_t id) const;
    /// Anim `id`'s far range in metres: `+0x08` × 0.001, or the reach × 1.25 when the stored value is not above the
    /// reach (0 among them); 0 without a record (docs/research/combat.md#targets).
    /// @orig 0x00254508 AttackTable_GetFarRange (unknown)
    [[nodiscard]] float farRange(std::size_t id) const;

    /// Replaces anim `id`'s damage, as the character class's damage table does to a human's copy of the list when
    /// it is made (the original's setter is `0x002548c8`); returns false for an id without a record. The damage
    /// measured at runtime is the overridden one: the file's differs (Rembrandt's S1 is 40 in the file, 17 in play).
    bool setDamage(std::size_t id, std::int16_t damage);

  private:
    std::vector<AnimRange> m_records;
};

/// The damage entries of a character class (`CfgChar`, class record `+0xb8`).
inline constexpr std::size_t kClassDamageEntries = 45;

/// Writes a character class's damage `values` (up to kClassDamageEntries, by class index) over `list`, as the
/// original does when a human is made: each index names the anim ids it sets (docs/research/combat.md#damage-table);
/// a value of 0 keeps the list's own. With `playerPercent` (the Warrior class byte `+0x06`, 115 for class 6; 0 for a
/// human that is not a player) each value is first scaled to `int(value × percent × 0.01 + 0.5)` in single-precision
/// floats. Returns how many records it set.
/// @orig 0x002548f0 AnimRange_ApplyClassDamage (unknown)
int applyClassDamage(AnimRangeList& list, std::span<const std::int16_t> values, int playerPercent);

} // namespace coney::combat
