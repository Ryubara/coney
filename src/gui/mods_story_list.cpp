// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the Mods screen's story view; the original has no mods.
#include "gui/mods_story_list.h"

#include <utility>

namespace coney::gui {

ModsStoryList::ModsStoryList(story::StoryGraph seed, std::vector<story::StoryMod> mods,
                             std::filesystem::path enabledFile)
    : m_seed(std::move(seed)), m_mods(std::move(mods)), m_file(std::move(enabledFile)) {
    m_merge = story::mergeStory(m_seed, m_mods);
}

ModsStoryList::Result ModsStoryList::handle(MenuCommand) { return Result::Nothing; }

void ModsStoryList::rebuild(std::string_view) {}
bool ModsStoryList::move(bool) { return false; }
void ModsStoryList::choose(const story::StoryGroup&, const std::vector<std::string>&, std::string_view) {}
void ModsStoryList::save() {}

} // namespace coney::gui
