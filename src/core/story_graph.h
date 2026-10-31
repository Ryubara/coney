// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/sha256.h"

namespace coney::story {

/// The hub's level number: a hub chapter is this level entered at the chapter's checkpoint.
inline constexpr int kHubLevel = 95;

/// One step of the story chain: a mission (a level entered at checkpoint 1) or a hub chapter (`level95` at a
/// checkpoint).
///
/// Research: docs/guides/studio/mission-mods.md#graph, docs/research/story-progress.md#order
struct StoryStep {
    /// What kind of step it is.
    enum class Kind : std::uint8_t {
        Mission,    ///< A level entered at checkpoint 1.
        HubChapter, ///< The hub (kHubLevel) entered at the chapter's checkpoint.
    };

    Kind kind = Kind::Mission;
    /// A mission's level number; kHubLevel for a hub chapter.
    int level = 0;
    /// A hub chapter's number (1-12), its checkpoint; 0 for a mission.
    int chapter = 0;
    /// A side step is reached by its own route, never from the step before it: `level64`, the last Armies stage, which
    /// sits in the chain only so that it counts as later than `level84`. No hub chapter offers a side step.
    bool sideStep = false;

    /// A mission: `level` entered at checkpoint 1.
    [[nodiscard]] static StoryStep mission(int level, bool sideStep = false) {
        return {.kind = Kind::Mission, .level = level, .chapter = 0, .sideStep = sideStep};
    }
    /// Hub chapter `chapter`: the hub entered at that checkpoint.
    [[nodiscard]] static StoryStep hubChapter(int chapter) {
        return {.kind = Kind::HubChapter, .level = kHubLevel, .chapter = chapter, .sideStep = false};
    }
    /// The step's id: an original mission's level name (`level80`), a hub chapter's `hub:<n>`.
    [[nodiscard]] std::string id() const;
    /// The checkpoint the step is entered at: the chapter for a hub chapter, 1 for a mission.
    [[nodiscard]] int checkpoint() const { return kind == Kind::HubChapter ? chapter : 1; }

    friend bool operator==(const StoryStep&, const StoryStep&) = default;
};

/// What `runNextMission` loads: a level at a checkpoint.
struct StoryLoad {
    int level = 0;      ///< The level number (`MenuLoadLevel("level" .. level)`).
    int checkpoint = 1; ///< The checkpoint set first (`SetCheckPoint`).

    friend bool operator==(const StoryLoad&, const StoryLoad&) = default;
};

/// One row of `runNextMission`'s table: from the latest story mission complete (0: none) to what loads next.
struct NextMissionRow {
    int from = 0;
    StoryLoad next;

    friend bool operator==(const NextMissionRow&, const NextMissionRow&) = default;
};

/// One row of the hub's `fRunMission`: the mission a chapter's last action loads, if any.
struct HubOffer {
    int chapter = 0;
    std::optional<int> level; ///< Nothing: the chapter offers no mission (chapters 11 and 12 in the original).

    friend bool operator==(const HubOffer&, const HubOffer&) = default;
};

/// The story's order as Coney's own data: a chain of steps, seeded with exactly the original's order
/// (StoryGraph::original()), from which the three tables the original's scripts keep are views: `runNextMission`'s
/// rows, `findLastMission`'s list and the hub's `fRunMission` offers. The queries nextLoad(), lastMission() and offer()
/// are what Coney's `runNextMission` and `findLastMission` natives and the hub's offer ask of it.
///
/// **Ownership (networking checklist):** simulation state of the story session, fixed while it runs: built once when
/// the session starts and never changed, so every machine of a co-op session holds the same chain; hash() is what a
/// session compares. Plain data, copied and compared by value.
///
/// Research: docs/guides/studio/mission-mods.md#story-graph, docs/research/story-progress.md
class StoryGraph {
  public:
    /// An empty chain: nothing loads and no mission is found.
    StoryGraph() = default;
    /// A chain of `steps`, in story order. The steps are taken as they are: merging and its refusals are not this
    /// class's.
    explicit StoryGraph(std::vector<StoryStep> steps) : m_steps(std::move(steps)) {}

    /// The original's chain: 18 missions and `level64`, joined by hub chapters 1-12, 31 steps.
    [[nodiscard]] static StoryGraph original();

    /// The steps, in story order.
    [[nodiscard]] std::span<const StoryStep> steps() const { return m_steps; }

    /// `runNextMission`'s table: 0 to the first step, then each mission to the step after it, in chain order (a mission
    /// with no step after it has no row).
    [[nodiscard]] std::vector<NextMissionRow> runNextMissionRows() const;
    /// `findLastMission`'s list: the missions' levels, latest first.
    [[nodiscard]] std::vector<int> findLastMissionLevels() const;
    /// The hub's `fRunMission`: each hub chapter in chapter order with the mission after it, none when the step after
    /// it is not a mission, is a side step or does not exist.
    [[nodiscard]] std::vector<HubOffer> hubOffers() const;

    /// `findLastMission()`: the latest mission in the chain that `isComplete` says is complete, else 0.
    [[nodiscard]] int lastMission(const std::function<bool(int level)>& isComplete) const;
    /// `runNextMission`'s row for `lastLevel` (0: none complete): what loads next, or nothing when the level has no
    /// row (the original's missing row, after which nothing loads).
    [[nodiscard]] std::optional<StoryLoad> nextLoad(int lastLevel) const;
    /// `fRunMission[chapter].level`: the mission hub chapter `chapter` offers; nothing when it offers none or the chain
    /// has no such chapter.
    [[nodiscard]] std::optional<int> offer(int chapter) const;

    /// SHA-256 of the chain in a fixed text form (a version line, then one line per step: its id, level, checkpoint
    /// and side-step mark), the same on every machine and build; what a co-op session compares.
    [[nodiscard]] Sha256Digest hash() const;

  private:
    std::vector<StoryStep> m_steps;
};

} // namespace coney::story
