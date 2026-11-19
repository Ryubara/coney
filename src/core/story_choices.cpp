// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the player's story merge choices in enabled.toml; the original has no mods.
#include "core/story_choices.h"

namespace coney::story {

std::expected<std::vector<StoryChoice>, Error> parseStoryChoices(std::string_view) {
    return std::vector<StoryChoice>{};
}

std::string withStoryChoices(std::string_view enabledText, std::span<const StoryChoice>) {
    return std::string(enabledText);
}

std::expected<std::vector<StoryChoice>, Error> loadStoryChoices(const std::filesystem::path&) {
    return std::vector<StoryChoice>{};
}

std::expected<void, Error> saveStoryChoices(const std::filesystem::path&, std::span<const StoryChoice>) { return {}; }

} // namespace coney::story
