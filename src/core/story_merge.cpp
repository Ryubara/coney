// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_merge.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coney::story {

namespace {

// A mission waiting for its turn: its story id, its mod and its manifest entry.
struct Pending {
    std::string id;
    std::string mod;
    const StoryMission* mission = nullptr;

    // The step it is placed after or in the place of, as the manifest names it.
    [[nodiscard]] const std::string& anchor() const {
        return mission->replace.empty() ? mission->after : mission->replace;
    }
};

// What one turn of a mission did.
enum class Turn : std::uint8_t { Placed, Waiting, Refused };

// The chain being merged and what the merge remembers about the steps it placed.
class Merger {
  public:
    explicit Merger(const StoryGraph& seed) : m_chain(seed.steps().begin(), seed.steps().end()) {}

    // Gives `mission` its turn: placed, refused with its message, or waiting for an anchor not placed yet.
    Turn take(const Pending& mission) {
        const StoryMission& entry = *mission.mission;
        const bool replacing = !entry.replace.empty();
        const std::string& named = mission.anchor();
        if (indexOf(mission.id) || placed(mission.id)) {
            return refuse(mission, std::format("{}: already in the story", mission.id));
        }
        if (replacing && m_replacedBy.contains(named)) {
            return refuse(mission, std::format("{}: '{}' is already replaced by '{}'", mission.id, named,
                                               m_replacedBy.at(named)));
        }
        const std::string anchor = resolve(named);
        const std::optional<std::size_t> at = indexOf(anchor);
        if (!at) {
            return Turn::Waiting;
        }
        if (replacing && m_chain[*at].kind == StoryStep::Kind::HubChapter) {
            return refuse(mission, std::format("{}: '{}' is a hub chapter, not a mission", mission.id, named));
        }
        // The before guard is read now, at the mission's turn: a mod placed earlier may have moved in.
        if (!entry.before.empty()) {
            const std::string found = *at + 1 < m_chain.size() ? m_chain[*at + 1].id() : std::string{"nothing"};
            if (found != resolve(entry.before)) {
                return refuse(mission, std::format("{}: expected '{}' after '{}', found '{}'", mission.id, entry.before,
                                                   named, found));
            }
        }
        if (replacing) {
            replaceAt(*at, mission.id);
        } else {
            insertAfter(*at, mission.id);
        }
        m_out.placed.push_back({.id = mission.id, .mod = mission.mod, .mission = entry});
        return Turn::Placed;
    }

    // Refuses every mission still waiting once no turn places anything: the missions of each cycle together, and
    // each other one for its missing or refused anchor.
    void refuseWaiting(const std::vector<Pending>& waiting) {
        std::map<std::string, const Pending*> byId;
        for (const Pending& mission : waiting) {
            byId.emplace(mission.id, &mission);
        }
        for (const Pending& mission : waiting) {
            if (m_refused.contains(mission.id)) {
                continue;
            }
            if (const auto cycle = cycleFrom(mission, byId)) {
                std::string message = "story cycle: ";
                for (const std::string& id : *cycle) {
                    message += id + ", ";
                }
                message += cycle->front();
                for (const std::string& id : *cycle) {
                    m_refused.insert(id);
                }
                m_out.refusals.push_back({.missions = *cycle, .message = std::move(message)});
                continue;
            }
            const std::string anchor = resolve(mission.anchor());
            if (byId.contains(anchor) || m_refused.contains(anchor)) {
                refuse(mission, std::format("{}: story step '{}' was refused", mission.id, mission.anchor()));
            } else {
                refuse(mission, std::format("{}: no story step '{}'", mission.id, mission.anchor()));
            }
        }
    }

    // The merge's outcome, with the chain as it now stands.
    StoryMerge finish(bool modded) && {
        m_out.graph = StoryGraph(std::move(m_chain));
        m_out.modded = modded;
        return std::move(m_out);
    }

  private:
    // The step `name` means now: a replaced mission's replacement (and its replacement's), else `name` itself.
    [[nodiscard]] std::string resolve(std::string name) const {
        for (auto found = m_replacedBy.find(name); found != m_replacedBy.end(); found = m_replacedBy.find(name)) {
            name = found->second;
        }
        return name;
    }

    // Where the step `id` is in the chain.
    [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view id) const {
        for (std::size_t index = 0; index < m_chain.size(); ++index) {
            if (m_chain[index].id() == id) {
                return index;
            }
        }
        return std::nullopt;
    }

    // Whether a mission `id` was already placed (it may have been replaced since).
    [[nodiscard]] bool placed(std::string_view id) const {
        return std::ranges::any_of(m_out.placed, [id](const PlacedMission& mission) { return mission.id == id; });
    }

    // Whether the step `id` was placed after `anchor`, directly or after a step that was: the group of steps a later
    // mission on the same anchor goes after.
    [[nodiscard]] bool placedAfter(const std::string& id, const std::string& anchor) const {
        for (auto parent = m_parent.find(id); parent != m_parent.end(); parent = m_parent.find(parent->second)) {
            if (resolve(parent->second) == anchor) {
                return true;
            }
        }
        return false;
    }

    // Places mission `id` after the step at `at` and after the group already placed there; after a hub chapter, the
    // mission opens a new chapter at that chapter's checkpoint, which follows it.
    void insertAfter(std::size_t at, const std::string& id) {
        const StoryStep anchor = m_chain[at];
        const std::string anchorId = anchor.id();
        std::size_t position = at + 1;
        while (position < m_chain.size() && placedAfter(m_chain[position].id(), anchorId)) {
            ++position;
        }
        const auto offset = static_cast<std::ptrdiff_t>(position);
        m_chain.insert(m_chain.begin() + offset, StoryStep::modMission(id));
        m_parent[id] = anchorId;
        if (anchor.kind == StoryStep::Kind::HubChapter) {
            std::string chapterId = std::format("hub:{}.{}", anchor.chapter, id);
            m_parent[chapterId] = id;
            m_chain.insert(m_chain.begin() + offset + 1,
                           StoryStep::modHubChapter(anchor.chapter, std::move(chapterId)));
        }
    }

    // Puts mission `id` in the place of the mission at `at`, entered the same way (a side step stays one).
    void replaceAt(std::size_t at, const std::string& id) {
        const std::string replaced = m_chain[at].id();
        if (const auto parent = m_parent.find(replaced); parent != m_parent.end()) {
            m_parent[id] = parent->second;
        }
        m_chain[at] = StoryStep::modMission(id, m_chain[at].sideStep);
        m_replacedBy[replaced] = id;
    }

    // Records the refusal of `mission` with `message`.
    Turn refuse(const Pending& mission, std::string message) {
        m_refused.insert(mission.id);
        m_out.refusals.push_back({.missions = {mission.id}, .message = std::move(message)});
        return Turn::Refused;
    }

    // The cycle of waiting missions that `start` is in, from `start` along each one's anchor; nothing when following
    // the anchors never comes back to it.
    [[nodiscard]] std::optional<std::vector<std::string>>
    cycleFrom(const Pending& start, const std::map<std::string, const Pending*>& byId) const {
        std::vector<std::string> path{start.id};
        const Pending* current = &start;
        while (path.size() <= byId.size()) {
            const auto next = byId.find(resolve(current->anchor()));
            if (next == byId.end()) {
                return std::nullopt;
            }
            if (next->first == start.id) {
                return path;
            }
            path.push_back(next->first);
            current = next->second;
        }
        return std::nullopt;
    }

    std::vector<StoryStep> m_chain;
    std::map<std::string, std::string> m_parent;     // a placed step's id -> the step it was placed after
    std::map<std::string, std::string> m_replacedBy; // a replaced mission's id -> its replacement's
    std::set<std::string> m_refused;                 // every mission id refused so far
    StoryMerge m_out;
};

} // namespace

std::vector<StoryGroup> storyGroups(std::span<const StoryMod> storyMods, std::span<const StoryChoice> choices) {
    // 1. Every mission under its kind and anchor, in turn order, and the package hash of its mod.
    std::vector<StoryGroup> groups;
    std::map<std::string, std::string> hashOf;
    for (const StoryMod& mod : storyMods) {
        for (const StoryMission& mission : mod.story.missions) {
            const std::string id = std::format("{}.{}", mod.name, mission.id);
            hashOf.emplace(id, mod.packageHash);
            const GroupKind kind = mission.replace.empty() ? GroupKind::Anchor : GroupKind::Replacement;
            const std::string& anchor = mission.replace.empty() ? mission.after : mission.replace;
            const auto group = std::ranges::find_if(
                groups, [&](const StoryGroup& known) { return known.kind == kind && known.anchor == anchor; });
            if (group == groups.end()) {
                groups.push_back({.kind = kind, .anchor = anchor, .defaultOrder = {id}});
            } else {
                group->defaultOrder.push_back(id);
            }
        }
    }
    // 2. A single mission is no group.
    std::erase_if(groups, [](const StoryGroup& group) { return group.defaultOrder.size() < 2; });
    // 3. The player's choice for each group, when it still holds: the group's own missions, in the packages the
    // choice was made between. One that names the group but no longer holds leaves it stale.
    for (StoryGroup& group : groups) {
        group.order = group.defaultOrder;
        const auto named = std::ranges::find_if(choices, [&](const StoryChoice& choice) {
            return choice.kind == group.kind && choice.anchor == group.anchor;
        });
        if (named == choices.end()) {
            continue;
        }
        const bool sameMissions = named->members.size() == group.defaultOrder.size() &&
                                  std::ranges::all_of(named->members, [&](const StoryChoiceMember& member) {
                                      return std::ranges::count(group.defaultOrder, member.mission) == 1 &&
                                             hashOf.at(member.mission) == member.packageHash;
                                  });
        if (!sameMissions) {
            group.stale = true;
            continue;
        }
        group.order.clear();
        for (const StoryChoiceMember& member : named->members) {
            group.order.push_back(member.mission);
        }
        group.chosen = true;
    }
    return groups;
}

StoryMerge mergeStory(const StoryGraph& seed, std::span<const StoryMod> storyMods,
                      std::span<const StoryChoice> choices) {
    // 1. Every mission in turn order: mods in load order, each mod's missions in manifest order.
    std::vector<Pending> waiting;
    for (const StoryMod& mod : storyMods) {
        for (const StoryMission& mission : mod.story.missions) {
            waiting.push_back({.id = std::format("{}.{}", mod.name, mission.id), .mod = mod.name, .mission = &mission});
        }
    }
    // 2. The player's choices: the missions of a group keep their slots in the turn order and take them in the chosen
    // order, so the first of two on one anchor is placed nearest it and the first of two replacements takes the place.
    std::vector<StoryGroup> groups = storyGroups(storyMods, choices);
    for (const StoryGroup& group : groups) {
        if (!group.chosen) {
            continue;
        }
        std::vector<std::size_t> slots;
        std::map<std::string, Pending> members;
        for (std::size_t slot = 0; slot < waiting.size(); ++slot) {
            if (std::ranges::find(group.order, waiting[slot].id) != group.order.end()) {
                slots.push_back(slot);
                members.emplace(waiting[slot].id, waiting[slot]);
            }
        }
        for (std::size_t i = 0; i < slots.size() && i < group.order.size(); ++i) {
            waiting[slots[i]] = members.at(group.order[i]);
        }
    }
    // 3. Turns until none places or refuses anything: after each one that does, the earliest waiting mission goes
    // again first, so a mission waiting for a later mod's anchor is placed as soon as the anchor is.
    Merger merger(seed);
    for (bool moved = true; moved;) {
        moved = false;
        for (auto mission = waiting.begin(); mission != waiting.end(); ++mission) {
            if (merger.take(*mission) != Turn::Waiting) {
                waiting.erase(mission);
                moved = true;
                break;
            }
        }
    }
    // 4. What still waits is refused: cycles, missing anchors and anchors on refused missions.
    merger.refuseWaiting(waiting);
    StoryMerge merged = std::move(merger).finish(!storyMods.empty());
    merged.groups = std::move(groups);
    return merged;
}

} // namespace coney::story
