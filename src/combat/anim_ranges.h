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
    std::int16_t kind = 0;   ///< `+0x0c`: the hit kind kept with pending damage (inferred).
    std::uint16_t flags = 0; ///< `+0x0e`: the playback-rate flags.
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
    /// Anim `id`'s far range in metres: `+0x08` × 0.001, or the reach × 1.25 when that is 0; 0 without a record.
    [[nodiscard]] float farRange(std::size_t id) const;

    /// Replaces anim `id`'s damage, as the character class's damage table does to a human's copy of the list when
    /// it is made (the original's setter is `0x002548c8`); returns false for an id without a record. The damage
    /// measured at runtime is the overridden one: the file's differs (Rembrandt's S1 is 40 in the file, 17 in play).
    bool setDamage(std::size_t id, std::int16_t damage);

  private:
    std::vector<AnimRange> m_records;
};

} // namespace coney::combat
