// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/story_natives.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "core/error.h"
#include "core/story_chapter.h"
#include "scripting/hub_chapter.h"
#include "scripting/lua_libraries.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "warriors/level_table.h"
#include "warriors/mod_story_progress.h"

namespace coney::script {

namespace {

using Results = std::expected<std::vector<Value>, Error>;

// Calls the global `name` with `args`, as a script's call of that name does: the name is looked up at the call, and a
// binding Coney lacks is skipped (the game state's calls of nil are no-ops).
// @coney-only Coney's natives over the story graph (docs/guides/studio/mission-mods.md#graph-natives)
Results callGlobal(LuaVm& vm, std::string_view name, std::span<const Value> args = {}) {
    return vm.call(vm.global(name), args);
}

// The latest mission complete, or the error of the completion check that failed (which stops the walk, as an error
// stops the Lua's findLastMission).
struct Latest {
    const story::StoryStep* step = nullptr;
    std::optional<Error> error;
};

// latestMission() over `graph`, an original mission asked of the scripts' own `UM_IsLevelComplete(level)` (the call
// the Lua's findLastMission makes, in the same order), a mod mission of its story entry in `progress`.
// @coney-only Coney's natives over the story graph (docs/guides/studio/mission-mods.md#graph-natives)
Latest latestComplete(LuaVm& vm, const story::StoryGraph& graph, const ModStoryProgress& progress) {
    Latest found;
    found.step = latestMission(
        graph,
        [&vm, &found](int level) {
            if (found.error) {
                return false;
            }
            const std::vector<Value> args{Value(static_cast<double>(level))};
            const Results complete = callGlobal(vm, "UM_IsLevelComplete", args);
            if (!complete) {
                found.error = complete.error();
                return false;
            }
            // Lua 4.0's truth: anything but nil.
            return !complete->empty() && !complete->front().isNil();
        },
        progress);
    return found;
}

// The `findLastMission()` native: the latest mission complete's level number, else 0.
// @coney-only replaces `findLastMission` (`global.lua:f62`) with the story graph's walk
NativeFunction findLastMissionNative(ScriptSystem& scripts, const story::StoryGraph& graph,
                                     const ModStoryProgress& progress) {
    return [&scripts, &graph, &progress](std::span<const Value> /*args*/) -> Results {
        const Latest found = latestComplete(scripts.vm(), graph, progress);
        if (found.error) {
            return std::unexpected(*found.error);
        }
        return std::vector<Value>{Value(found.step != nullptr ? static_cast<double>(found.step->level) : 0.0)};
    };
}

// The step `runNextMission` loads after `LastLevel`'s value `last`: a whole number with a step after it in `graph`.
// @coney-only replaces `runNextMission`'s table (`global.lua:f61`) with the story graph's
std::optional<story::StoryLoad> loadAfter(const story::StoryGraph& graph, const Value& last) {
    const std::optional<double> number = last.number();
    // The Lua indexes its table with the value: only a whole number that is a row's key finds one. The bound keeps
    // the cast to int defined; every level number is far below it.
    if (!number || std::trunc(*number) != *number || std::abs(*number) > 1'000'000.0) {
        return std::nullopt;
    }
    return graph.nextLoad(static_cast<int>(*number));
}

// The `runNextMission(arg)` native, described at installStoryNatives().
// @coney-only replaces `runNextMission` (`global.lua:f61`) with the story graph's
NativeFunction runNextMissionNative(ScriptSystem& scripts, const story::StoryGraph& graph) {
    return [&scripts, &graph](std::span<const Value> args) -> Results {
        LuaVm& vm = scripts.vm();
        // LastLevel from findLastMission() when 0 or nil (global.lua's main chunk sets it to 0 at every level load).
        Value last = vm.global("LastLevel");
        if (last.isNil() || last.number() == 0.0) {
            const Results found = callGlobal(vm, "findLastMission");
            if (!found) {
                return std::unexpected(found.error());
            }
            last = found->empty() ? Value() : found->front();
            vm.setGlobal("LastLevel", last);
        }
        // No step after it: nothing loads, as after the Lua's call of a name no binding has.
        const std::optional<story::StoryLoad> load = loadAfter(graph, last);
        if (!load) {
            scripts.log(std::format("runNextMission: no story step after level {}; nothing loads", luaToString(last)));
            return std::vector<Value>{};
        }
        // What the Lua's row does, each call by its name.
        const std::vector<Value> checkpoint{Value(static_cast<double>(load->checkpoint))};
        const std::vector<Value> level{Value(std::format("level{}", load->level))};
        for (const auto& [name, callArgs] : {std::pair{"SetCheckPoint", std::span<const Value>(checkpoint)},
                                             std::pair{"SoundStopMusicTrack", std::span<const Value>()},
                                             std::pair{"MenuLoadLevel", std::span<const Value>(level)}}) {
            if (const Results called = callGlobal(vm, name, callArgs); !called) {
                return std::unexpected(called.error());
            }
        }
        if (!args.empty() && args[0].number() == 1.0) {
            const std::vector<Value> kind{Value(4.0)};
            if (const Results called = callGlobal(vm, "HUDLaunchMissionComplete", kind); !called) {
                return std::unexpected(called.error());
            }
        }
        return std::vector<Value>{};
    };
}

// A mod mission's level record: `level<n>` with level number n at `index`, its title as the menus' names. The rest of
// the record's values stay 0: whether mod missions join the mission menu and the statistics is an open question
// (mission-mods.md#graph-questions).
// @coney-only the session's level records of mod missions (docs/guides/studio/mission-mods.md#graph-natives)
LevelRecord modLevelRecord(std::size_t index, int number, const story::StoryMission* mission) {
    LevelRecord record;
    record.id = static_cast<double>(index);
    record.name = std::format("level{}", number);
    record.worldName = record.name;
    record.number = number;
    if (mission != nullptr) {
        record.fourthName = mission->title;
        record.secondName =
            mission->label ? std::format("{}:{}", *mission->label, mission->title) : std::format(":{}", mission->title);
    }
    return record;
}

} // namespace

void installStoryNatives(ScriptSystem& scripts, const story::StoryGraph& graph, const ModStoryProgress& progress) {
    if (!scripts.exists()) {
        return;
    }
    LuaVm& vm = scripts.vm();
    vm.registerFunction("findLastMission", findLastMissionNative(scripts, graph, progress));
    vm.registerFunction("runNextMission", runNextMissionNative(scripts, graph));
}

void enterHub(ScriptSystem& scripts, const story::StoryMerge& merge, const ModStoryProgress& progress, int checkpoint) {
    if (!scripts.exists()) {
        return;
    }
    const Latest found = latestComplete(scripts.vm(), merge.graph, progress);
    if (found.error) {
        scripts.log(std::format("hub: the completion check failed ({}); the hub plays as for no mission complete",
                                found.error->message));
    }
    const std::string latest = found.step != nullptr && !found.error ? found.step->id() : std::string();
    const std::optional<story::HubChapterPlan> plan = story::planHubChapter(
        merge, checkpoint, latest.empty() ? std::nullopt : std::optional<std::string_view>(latest));
    if (plan) {
        applyHubChapter(scripts, *plan);
    }
}

StoryHooks::StoryHooks(ScriptSystem& scripts, GameState& state, story::StoryMerge merge)
    : m_scripts(scripts), m_state(state), m_merge(std::move(merge)) {}

void StoryHooks::attach() {
    m_scripts.setLevelEntryHook([this](std::string_view level, LevelEntryStage stage) {
        if (stage == LevelEntryStage::AfterGlobal) {
            afterGlobal(level);
        } else {
            afterLevel(level);
        }
    });
}

bool StoryHooks::numberMissions() {
    if (m_numbered) {
        return true;
    }
    if (m_state.levels.count() == 0) {
        return false;
    }
    // The numbers the records use, and the indices no record holds.
    std::vector<int> used;
    std::vector<std::size_t> freeIndices;
    for (std::size_t index = 0; index < LevelTable::kCapacity; ++index) {
        if (const LevelRecord* record = m_state.levels.at(index)) {
            used.push_back(static_cast<int>(record->number));
        } else {
            freeIndices.push_back(index);
        }
    }
    // Coney's stand-in: a mission needs a record too, so the pool is cut to the free records (17 on the NTSC-U disc's
    // table of 128); the design counts numbers only.
    std::vector<int> pool = story::levelNumberPool(used);
    if (pool.size() > freeIndices.size()) {
        pool.resize(freeIndices.size());
    }
    const std::size_t refusedBefore = m_merge.refusals.size();
    m_numbers = story::numberModMissions(m_merge, pool);
    for (std::size_t i = 0; i < m_numbers.size(); ++i) {
        const story::NumberedMission& numbered = m_numbers[i];
        const auto placed = std::ranges::find(m_merge.placed, numbered.id, &story::PlacedMission::id);
        const story::StoryMission* mission = placed != m_merge.placed.end() ? &placed->mission : nullptr;
        m_state.levels.set(modLevelRecord(freeIndices[i], numbered.level, mission));
        m_state.modStory.assign(static_cast<std::uint8_t>(numbered.level), numbered.id);
    }
    for (std::size_t i = refusedBefore; i < m_merge.refusals.size(); ++i) {
        m_scripts.log(std::format("story: {}", m_merge.refusals[i].message));
    }
    m_numbered = true;
    return true;
}

void StoryHooks::afterGlobal(std::string_view level) {
    numberMissions();
    installStoryNatives(m_scripts, m_merge.graph, m_state.modStory);
    // A story session starts at the first level after the front end: a loaded save's missing story mods are named.
    const bool frontEnd = level == kFrontEnd;
    if (!frontEnd && m_fromFrontEnd) {
        warnMissingStoryMods(m_state.modStory, m_merge.graph);
    }
    m_fromFrontEnd = frontEnd;
}

void StoryHooks::afterLevel(std::string_view level) {
    if (level == std::format("level{}", story::kHubLevel)) {
        enterHub(m_scripts, m_merge, m_state.modStory, static_cast<int>(std::trunc(m_state.checkPoint)));
    }
}

} // namespace coney::script
