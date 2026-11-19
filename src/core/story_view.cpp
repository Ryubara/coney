// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the Mods screen's story view; the original has no mods.
#include "core/story_view.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coney::story {

namespace {

// What the view needs to know about the mods and the merge, looked up by story id.
class Lookup {
  public:
    // Indexes the mods' missions and the merge's placed missions by story id; both outlive the lookup.
    Lookup(const StoryMerge& merge, std::span<const StoryMod> mods) : m_merge(merge), m_mods(mods) {
        for (const StoryMod& mod : mods) {
            for (const StoryMission& mission : mod.story.missions) {
                m_modOf.emplace(std::format("{}.{}", mod.name, mission.id), mod.name);
            }
        }
        for (const PlacedMission& placed : merge.placed) {
            m_placed.emplace(placed.id, &placed);
        }
    }

    // The placed mission `id`; null when the merge did not place it.
    [[nodiscard]] const PlacedMission* placed(const std::string& id) const {
        const auto found = m_placed.find(id);
        return found == m_placed.end() ? nullptr : found->second;
    }

    // The mod that holds the mission `id`; empty when none does.
    [[nodiscard]] std::string modOf(const std::string& id) const {
        const auto found = m_modOf.find(id);
        return found == m_modOf.end() ? std::string{} : found->second;
    }

    // What the screen calls mod `name`: its title, else the name.
    [[nodiscard]] std::string titleOf(const std::string& name) const {
        const auto found = std::ranges::find(m_mods, name, &StoryMod::name);
        return found == m_mods.end() || found->title.empty() ? name : found->title;
    }

    // The group of `kind` with `anchor` that holds the mission `id`; null when it is in none.
    [[nodiscard]] const StoryGroup* groupOf(GroupKind kind, const std::string& anchor, const std::string& id) const {
        const auto found = std::ranges::find_if(m_merge.groups, [&](const StoryGroup& group) {
            return group.kind == kind && group.anchor == anchor &&
                   std::ranges::find(group.order, id) != group.order.end();
        });
        return found == m_merge.groups.end() ? nullptr : &*found;
    }

    // How many missions of `group` the merge placed.
    [[nodiscard]] std::size_t placedIn(const StoryGroup& group) const {
        return static_cast<std::size_t>(
            std::ranges::count_if(group.order, [this](const std::string& id) { return placed(id) != nullptr; }));
    }

  private:
    const StoryMerge& m_merge;
    std::span<const StoryMod> m_mods;
    std::map<std::string, std::string> m_modOf;
    std::map<std::string, const PlacedMission*> m_placed;
};

// The mission id a mod's step stands for: a mission's own, and for the new hub chapter `hub:<n>.<mission>` its
// mission's.
std::string missionIdOf(const StoryStep& step) {
    if (step.kind == StoryStep::Kind::Mission) {
        return step.modId;
    }
    const auto dot = step.modId.find('.');
    return dot == std::string::npos ? step.modId : step.modId.substr(dot + 1);
}

} // namespace

std::vector<StoryViewRow> buildStoryView(const StoryMerge& merge, std::span<const StoryMod> storyMods) {
    const Lookup lookup(merge, storyMods);
    std::vector<StoryViewRow> rows;
    // 1. The chain, in order: an original step is one row, a mod's steps follow a row with the mod's title.
    std::string currentMod;
    for (const StoryStep& step : merge.graph.steps()) {
        if (step.modId.empty()) {
            currentMod.clear();
            rows.push_back({.kind = StoryViewRow::Kind::Original, .id = step.id(), .text = step.id()});
            continue;
        }
        const std::string missionId = missionIdOf(step);
        const PlacedMission* placed = lookup.placed(missionId);
        const std::string mod = placed ? placed->mod : lookup.modOf(missionId);
        if (mod != currentMod) {
            currentMod = mod;
            rows.push_back({.kind = StoryViewRow::Kind::ModTitle, .text = lookup.titleOf(mod), .mod = mod});
        }
        StoryViewRow row{.kind = StoryViewRow::Kind::ModMission, .id = step.id(), .mod = mod};
        if (step.kind == StoryStep::Kind::HubChapter) {
            row.text = std::format("Hub chapter {}", step.chapter);
            rows.push_back(std::move(row));
            continue;
        }
        row.text = placed && !placed->mission.title.empty() ? placed->mission.title : missionId;
        // The marks: a shared anchor, which can be swapped, and a replacement with the missions it was chosen over.
        if (placed && !placed->mission.after.empty()) {
            const StoryGroup* group = lookup.groupOf(GroupKind::Anchor, placed->mission.after, missionId);
            if (group != nullptr && lookup.placedIn(*group) >= 2) {
                row.action = StoryViewRow::Action::Swap;
                row.anchor = group->anchor;
                row.chosen = group->chosen;
            }
        }
        if (placed && !placed->mission.replace.empty()) {
            row.replaces = placed->mission.replace;
            if (const StoryGroup* group = lookup.groupOf(GroupKind::Replacement, row.replaces, missionId)) {
                std::ranges::copy_if(group->order, std::back_inserter(row.rivals),
                                     [&](const std::string& other) { return other != missionId; });
                row.chosen = group->chosen;
            }
        }
        rows.push_back(std::move(row));
    }
    // 2. What the merge refused, with its messages; a replacement that lost to another can be chosen.
    if (!merge.refusals.empty()) {
        rows.push_back({.kind = StoryViewRow::Kind::RefusedHeader});
    }
    for (const StoryRefusal& refusal : merge.refusals) {
        for (const std::string& id : refusal.missions) {
            StoryViewRow row{
                .kind = StoryViewRow::Kind::Refusal, .id = id, .text = refusal.message, .mod = lookup.modOf(id)};
            for (const StoryGroup& group : merge.groups) {
                const bool lost = group.kind == GroupKind::Replacement &&
                                  std::ranges::find(group.order, id) != group.order.end() && lookup.placedIn(group) > 0;
                if (lost) {
                    row.action = StoryViewRow::Action::Choose;
                    row.replaces = group.anchor;
                    row.chosen = group.chosen;
                }
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

} // namespace coney::story
