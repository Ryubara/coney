// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the Mods screen's story view; the original has no mods.
#include "gui/mods_story_list.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace coney::gui {

namespace {

// Whether the merge placed the mission `id`.
bool isPlaced(const story::StoryMerge& merge, const std::string& id) {
    return std::ranges::any_of(merge.placed, [&](const story::PlacedMission& mission) { return mission.id == id; });
}

// The package hash the mod of mission `id` has, as `enabled.toml` records it.
std::string hashOf(std::span<const story::StoryMod> mods, const std::string& id) {
    for (const story::StoryMod& mod : mods) {
        for (const story::StoryMission& mission : mod.story.missions) {
            if (std::format("{}.{}", mod.name, mission.id) == id) {
                return mod.packageHash;
            }
        }
    }
    return {};
}

} // namespace

ModsStoryList::ModsStoryList(story::StoryGraph seed, std::vector<story::StoryMod> mods,
                             std::filesystem::path enabledFile)
    : m_seed(std::move(seed)), m_mods(std::move(mods)), m_file(std::move(enabledFile)) {
    // 1. The choices the player kept; a file that cannot be read gives none and a message.
    if (!m_file.empty()) {
        auto loaded = story::loadStoryChoices(m_file);
        if (loaded) {
            m_choices = std::move(*loaded);
        } else {
            m_message = loaded.error().message;
        }
    }
    // 2. The merge with them, and the rows.
    rebuild({});
}

ModsStoryList::Result ModsStoryList::handle(MenuCommand command) {
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down:
        return move(command == MenuCommand::Down) ? Result::Moved : Result::Nothing;
    case MenuCommand::Left:
    case MenuCommand::Right:
        return Result::Nothing;
    case MenuCommand::Back:
        return Result::Closed;
    case MenuCommand::Accept:
        break;
    }
    if (!m_cursor) {
        return Result::Nothing;
    }
    const story::StoryViewRow row = m_rows[*m_cursor];
    const auto inGroup = [&](story::GroupKind kind, const std::string& anchor) {
        return std::ranges::find_if(m_merge.groups, [&](const story::StoryGroup& group) {
            return group.kind == kind && group.anchor == anchor &&
                   std::ranges::find(group.order, row.id) != group.order.end();
        });
    };
    if (row.action == story::StoryViewRow::Action::Swap) {
        // Swap with the next mission placed on the anchor, or with the one before when this is the last.
        const auto group = inGroup(story::GroupKind::Anchor, row.anchor);
        if (group == m_merge.groups.end()) {
            return Result::Nothing;
        }
        std::vector<std::string> order = group->order;
        const auto self = static_cast<std::size_t>(std::ranges::find(order, row.id) - order.begin());
        std::optional<std::size_t> other;
        for (std::size_t i = self + 1; i < order.size() && !other; ++i) {
            if (isPlaced(m_merge, order[i])) {
                other = i;
            }
        }
        for (std::size_t i = self; i > 0 && !other; --i) {
            if (isPlaced(m_merge, order[i - 1])) {
                other = i - 1;
            }
        }
        if (!other) {
            return Result::Nothing;
        }
        std::swap(order[self], order[*other]);
        choose(*group, order, row.id);
        return Result::Swapped;
    }
    if (row.action == story::StoryViewRow::Action::Choose) {
        // The refused replacement goes first; the others keep their order behind it.
        const auto group = inGroup(story::GroupKind::Replacement, row.replaces);
        if (group == m_merge.groups.end()) {
            return Result::Nothing;
        }
        std::vector<std::string> order{row.id};
        std::ranges::copy_if(group->order, std::back_inserter(order),
                             [&](const std::string& id) { return id != row.id; });
        choose(*group, order, row.id);
        return Result::Chose;
    }
    return Result::Nothing;
}

void ModsStoryList::rebuild(std::string_view keep) {
    m_merge = story::mergeStory(m_seed, m_mods, m_choices);
    m_rows = story::buildStoryView(m_merge, m_mods);
    m_cursor.reset();
    for (std::size_t i = 0; i < m_rows.size() && !m_cursor; ++i) {
        if (m_rows[i].selectable() && (keep.empty() || m_rows[i].id == keep)) {
            m_cursor = i;
        }
    }
    // The mission asked for is not a row (it was refused again): the first row it can stop on.
    for (std::size_t i = 0; i < m_rows.size() && !m_cursor; ++i) {
        if (m_rows[i].selectable()) {
            m_cursor = i;
        }
    }
}

bool ModsStoryList::move(bool down) {
    if (!m_cursor) {
        return false;
    }
    std::size_t index = *m_cursor;
    while (down ? index + 1 < m_rows.size() : index > 0) {
        index = down ? index + 1 : index - 1;
        if (m_rows[index].selectable()) {
            m_cursor = index;
            return true;
        }
    }
    return false;
}

void ModsStoryList::choose(const story::StoryGroup& group, const std::vector<std::string>& order,
                           std::string_view keep) {
    // 1. What `group` says is read before the rebuild replaces it: the choice for this kind and anchor is dropped, and
    // a new one put in its place unless the order is the load order, which needs no choice.
    const story::GroupKind kind = group.kind;
    const std::string anchor = group.anchor;
    const bool loadOrder = order == group.defaultOrder;
    const auto names = [&](const story::StoryChoice& choice) { return choice.kind == kind && choice.anchor == anchor; };
    const auto first = std::ranges::find_if(m_choices, names);
    const auto position = static_cast<std::size_t>(first - m_choices.begin());
    std::erase_if(m_choices, names);
    if (!loadOrder) {
        story::StoryChoice made{.kind = kind, .anchor = anchor, .members = {}};
        for (const std::string& id : order) {
            made.members.push_back({.mission = id, .packageHash = hashOf(m_mods, id)});
        }
        m_choices.insert(m_choices.begin() + static_cast<std::ptrdiff_t>(std::min(position, m_choices.size())),
                         std::move(made));
    }
    // 2. The merge and the rows with it, the cursor on the mission it was on, and the file.
    rebuild(keep);
    save();
}

void ModsStoryList::save() {
    if (m_file.empty()) {
        return;
    }
    // The save reads the file again: one whose choices do not read (broken from the start or edited while the list is
    // open) is not written over, and the choice then holds for this list only, until the screen is opened again and
    // reads the file's own.
    const auto saved = story::saveStoryChoices(m_file, m_choices);
    m_message = saved
                    ? std::string{}
                    : std::format("{}; the choice holds until the Mods screen is opened again", saved.error().message);
}

} // namespace coney::gui
