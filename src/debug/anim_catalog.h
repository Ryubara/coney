// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_clip.h"

namespace coney::characters {
class AnimSet;
}

// The Animation player's catalogue (docs/guides/debug-menu.md#animation-player): the clips an anim set can play, by
// anim id, with their names and lengths, grouped into categories for the menu. Coney's own tooling: the original has
// no such list. The anim ids and their `ANIM_*` names are docs/references/anim-ids.md; how a set answers an id is
// docs/research/characters.md#files and docs/research/formats/animation.md#where-clips-are-found.

namespace coney::debug {

/// The anim ids every character data maps to clips: 0 to 721 (docs/references/anim-ids.md).
inline constexpr std::size_t kAnimIdCount = 722;

/// The `ANIM_*` name the scripts (`royal.lua`) give anim `id`; empty past the last id.
[[nodiscard]] std::string_view animIdName(std::uint32_t id);

/// The menu's category of anim `id`: the first word of its `ANIM_*` name in title case (`ANIM_GRAB_START` is "Grab"),
/// with a word's forms together ("Mount" for MOUNT, MOUNTING and MOUNTED; "Arrest" for ARREST, ARRESTING and
/// ARRESTED); "Other" for an id with no name. **Coney's choice**: the research defines no categories of anim ids, and
/// the scripts' names are the one grouping every id has.
[[nodiscard]] std::string animCategory(std::uint32_t id);

/// One clip an anim set offers the Animation player.
struct AnimClipChoice {
    std::uint32_t id = 0; ///< The anim id, 0 to 721.
    std::string clip;     ///< The clip's name, from its descriptor.
    float seconds = 0.0F; ///< The clip's length at 30 frames a second.
    float rate = 1.0F;    ///< The set's playback rate for the id (`CfgAnimSpeeds` by its Anim Range List flags).
    bool generic = false; ///< The set answers the id from the generic character data, not its own.

    /// How long the clip plays at its rate times `speed`, seconds.
    [[nodiscard]] float playSeconds(float speed = 1.0F) const { return seconds / (rate * speed); }
};

/// Whether `clip` can play on the player's skeleton: it animates no bone past the 34 pose entries every character on
/// the disc shares (docs/research/characters.md, "Characters are skinned with 32 bones").
[[nodiscard]] bool playableOnPlayer(const anim::AnimClip& clip);

/// The clips `set` plays, one per anim id it answers with a clip the player's skeleton can play, in id order, with
/// each one's rate from the set.
[[nodiscard]] std::vector<AnimClipChoice> clipChoicesOf(const characters::AnimSet& set);

/// One category of a set's clips (animCategory()).
struct AnimCategoryClips {
    std::string name;
    std::vector<AnimClipChoice> clips; ///< In id order.
};

/// `clips` grouped by animCategory(), the categories in alphabetical order.
[[nodiscard]] std::vector<AnimCategoryClips> byCategory(const std::vector<AnimClipChoice>& clips);

} // namespace coney::debug
