// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "animation/anim_clip.h"
#include "characters/character_data.h"

// The clips one character can play, by anim id, with their playback rates and the speeds their root motion gives.
// Research: docs/research/characters.md#files, docs/research/formats/animation.md#playback-rate

namespace coney::characters {

/// The four rates `CfgAnimSpeeds(default, f800, f1000, f2000)` sets, as read at runtime in level99: a clip with no
/// rate flag plays at 0.75, flag 0x800 at 0.8, 0x1000 at 1.0, 0x2000 at 0.9. Coney has no config script run yet, so
/// these stand for the call.
inline constexpr std::array<float, 4> kAnimSpeeds{0.75F, 0.8F, 1.0F, 0.9F};
/// The rate flags of an Anim Range List record (`+0x0e`).
inline constexpr std::uint16_t kRangeRate800 = 0x0800;
inline constexpr std::uint16_t kRangeRate1000 = 0x1000;
inline constexpr std::uint16_t kRangeRate2000 = 0x2000;

/// **Coney's stand-in for the resource manager's default anim table** (resource manager `+0x70`, not researched):
/// the hash naming the character data resource that holds the generic clips (594 of them, the `gen_*` set). A
/// character's slot that says "use the default" is answered from it. Evidence (Coney's disc check): its clips for ids
/// 380, 407, 409, 410 and 411 give exactly the speeds read from Rembrandt at runtime (3.429, 1.585, 4.857, 7.801 and
/// 10.245 m/s), and Rembrandt's own data answers none of these ids.
inline constexpr std::uint32_t kGenericAnimDataHash = 0x9da2e531;

/// A character's clips by anim id: its own character data first, then the defaults.
class AnimSet {
  public:
    /// The clips of `own`, with the slots it leaves to the default answered from `defaults` (may be null: those ids
    /// then have no clip). Both must outlive the set.
    AnimSet(const CharacterData& own, const CharacterData* defaults) : m_own(&own), m_defaults(defaults) {}

    /// The clip for anim `id`, or null: CharacterData::animation(), then the defaults.
    [[nodiscard]] const anim::AnimClip* clip(std::size_t id) const;

    /// The rate flags of anim `id` from the character's own Anim Range List (`+0x0e` of its record); 0 when the list
    /// is too short.
    [[nodiscard]] std::uint16_t rangeFlags(std::size_t id) const;

    /// The playback rate a single clip of anim `id` gets: kAnimSpeeds by its rate flags.
    /// @orig 0x00104a38 Anim_RateMultiplier (unknown)
    [[nodiscard]] float rate(std::size_t id) const;

    /// The speed anim `id`'s clip moves at: its horizontal root displacement over its playing time
    /// (`sqrt(dx² + dy²) / (duration / rate)`); 0 without a clip or for an empty one.
    [[nodiscard]] float speed(std::size_t id) const;

  private:
    const CharacterData* m_own;
    const CharacterData* m_defaults;
};

} // namespace coney::characters
