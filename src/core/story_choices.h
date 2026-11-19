// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// The player's choices about the story merge, kept in `mods/enabled.toml`: the order of the missions placed on one
// anchor (the Mods screen's *Swap*) and which of two replacements of one mission takes its place. Coney's own; the
// original has no mods.
//
// Research: docs/guides/studio/mission-mods.md#graph-merge, docs/guides/studio/mods.md#installed

namespace coney::story {

/// What a group of missions that the merge has to put in some order shares.
enum class GroupKind : std::uint8_t {
    Anchor, ///< Missions with one `after` anchor: they are placed in the order given, the first nearest the anchor.
    Replacement, ///< Missions with one `replace` target: the first takes its place and the others are refused.
};

/// One mission of a choice, with the package hash of the mod it comes from: a choice holds while every mod it names is
/// the package the player chose between, as a script merge choice does ([Two mods patching one
/// script](docs/guides/studio/mission-mods.md#two-mods)).
struct StoryChoiceMember {
    std::string mission;     ///< The story id, `<mod>.<id>`.
    std::string packageHash; ///< The mod's package hash as `enabled.toml` records it; empty when it has none yet.

    friend bool operator==(const StoryChoiceMember&, const StoryChoiceMember&) = default;
};

/// The order the player chose for one group: the anchor (or replaced mission) as the manifests write it and every
/// mission of the group, in the chosen order.
struct StoryChoice {
    GroupKind kind = GroupKind::Anchor;
    std::string anchor;                     ///< The `after` or `replace` value the missions share.
    std::vector<StoryChoiceMember> members; ///< The group's missions in the chosen order, at least two.

    friend bool operator==(const StoryChoice&, const StoryChoice&) = default;
};

/// Reads the story choices of `enabled.toml`'s text: one `[[story.choice]]` table each, with `kind` (`"after"` or
/// `"replace"`), `anchor`, `missions` and `packages` (two arrays of strings of one length, at least two). Every other
/// table and key is skipped, so the rest of the file is read by its own reader; text with no choice gives none. Fails
/// with ErrorCode::Invalid on a broken line (`line N: ...`) and on a choice without its keys, with arrays of different
/// lengths or fewer than two missions, or with an unknown kind.
[[nodiscard]] std::expected<std::vector<StoryChoice>, Error> parseStoryChoices(std::string_view enabledText);

/// `enabledText` with its story choices replaced by `choices`: every `[[story.choice]]` table is dropped and one is
/// written for each choice at the end, and everything else (the mods, their order, their approvals, comments) is left
/// as it was. parseStoryChoices() reads the result back to `choices`.
[[nodiscard]] std::string withStoryChoices(std::string_view enabledText, std::span<const StoryChoice> choices);

/// The story choices in the file `enabledFile`. A file that does not exist holds none. Fails with ErrorCode::Io when it
/// cannot be read and as parseStoryChoices() does, naming the file.
[[nodiscard]] std::expected<std::vector<StoryChoice>, Error> loadStoryChoices(const std::filesystem::path& enabledFile);

/// Writes `choices` into the file `enabledFile` as withStoryChoices() does, making the file (and its folder) when it is
/// not there and replacing it in one step, so a crash leaves the old file or the new one. Fails with ErrorCode::Io.
[[nodiscard]] std::expected<void, Error> saveStoryChoices(const std::filesystem::path& enabledFile,
                                                          std::span<const StoryChoice> choices);

} // namespace coney::story
