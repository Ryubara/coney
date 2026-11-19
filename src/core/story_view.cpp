// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the Mods screen's story view; the original has no mods.
#include "core/story_view.h"

namespace coney::story {

std::vector<StoryViewRow> buildStoryView(const StoryMerge&, std::span<const StoryMod>) { return {}; }

} // namespace coney::story
