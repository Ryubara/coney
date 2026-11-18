// SPDX-License-Identifier: GPL-3.0-or-later
// The hub chapters that mod missions open, in the hub's scripts (docs/guides/studio/mission-mods.md#hub-chapters):
// Coney's own, with no counterpart in the original. The hub and its chapter here are synthetic scripts of our own that
// use the names the original's hub reads (docs/research/story-progress.md#hub), compiled by Coney's own compiler.
#include "scripting/hub_chapter.h"

#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/story_chapter.h"
#include "core/story_graph.h"
#include "scripting/lua_chunk.h"
#include "scripting/lua_compiler.h"
#include "scripting/lua_libraries.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"

using coney::Error;
using coney::ErrorCode;
using coney::script::applyHubChapter;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;
using coney::story::HubChapterPlan;
using coney::story::StoryStep;

namespace {

// The hub: three action kinds, the giver's flag and level for chapters 6 and 11, a RunLevel that runs whichever of the
// four configuration functions exist and notes each action (index, kind, flag) and the crew, and a Main that asks for
// the chapter file with RunLevel as its callback.
constexpr std::string_view kHub = R"(
MA_REDCIRCLE, MA_CUTSCENE, MA_LOADLEVEL = 1, 2, 9
fRunMission = {[6] = {flag = 606, level = 86}, [11] = {}}
function RunLevel()
    local g = globals()
    for _, name in {"CfgMissionActions", "CfgMissionHumans", "CfgSceneTables", "CfgMissionTable"} do
        if g[name] then g[name]() end
    end
    local n = 1
    while MissionAction[n] do
        local a = MissionAction[n]; Note(n, a.actionType, a.flag or 0); n = n + 1
    end
    Note("crew", NumWarriors, RadioTrack)
end
function Main() checkpoint = GetCheckPoint(); preLoadFile("level95_chapter" .. checkpoint, "RunLevel") end
Main()
)";

// A chapter: the crew and radio, then actions configured once (a red circle at the giver with a story event, a scene
// with its cast, the load), and scene and side-mission configuration that note themselves.
constexpr std::string_view kChapter = R"(
NumWarriors, RadioTrack, MissionAction = 7, 3, {}
function CfgMissionActions()
    local giver = fRunMission[checkpoint].flag
    MissionAction = {{actionType = MA_REDCIRCLE, flag = giver, event = 1},
                     {actionType = MA_CUTSCENE, sceneTable = {}}, {actionType = MA_LOADLEVEL}}
    globals().CfgMissionActions = nil
end
function CfgMissionHumans() MissionAction[2].sceneTable.Humans = {1} end
function CfgSceneTables() Note("scenes") end
function CfgMissionTable() Note("submission") end
)";

// A mod's chapter script: a scene of its own, then the load.
constexpr std::string_view kModChapter = R"(
function CfgMissionActions()
    MissionAction = {{actionType = MA_CUTSCENE, sceneTable = {}}, {actionType = MA_LOADLEVEL}}
    globals().CfgMissionActions = nil
end
)";

// `source` compiled into a chunk in the game's layout.
std::vector<std::byte> chunkOf(std::string_view source) {
    auto compiled = coney::script::compileLua(source, "=test");
    REQUIRE(compiled.has_value());
    return compiled ? coney::script::writeLuaChunk(*compiled) : std::vector<std::byte>{};
}

// A script system over the synthetic hub at `checkpoint`, with `Note(...)` recording its calls, `preLoadFile` and
// `GetCheckPoint`.
struct Hub {
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<std::string> notes;
    int checkpoint = 6;
    ScriptSystem scripts;

    Hub()
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
                  const auto found = files.find(name);
                  if (found == files.end()) {
                      return coney::fail(ErrorCode::NotFound, std::string(name) + ": no such script");
                  }
                  return found->second;
              },
              [this](ScriptSystem& system, LuaVm& vm) {
                  vm.registerFunction("Note",
                                      [this](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
                                          std::string note;
                                          for (std::size_t i = 0; i < args.size(); ++i) {
                                              note += (i == 0 ? "" : ",") + coney::script::luaToString(args[i]);
                                          }
                                          notes.push_back(note);
                                          return std::vector<Value>{};
                                      });
                  vm.registerFunction("GetCheckPoint",
                                      [this](std::span<const Value>) -> std::expected<std::vector<Value>, Error> {
                                          return std::vector<Value>{Value(static_cast<double>(checkpoint))};
                                      });
                  vm.registerFunction(
                      "preLoadFile",
                      [&system](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
                          const auto name = !args.empty() ? args[0].string() : std::nullopt;
                          const auto callback = args.size() > 1 ? args[1].string() : std::nullopt;
                          system.preload(std::string(name.value_or("")), std::string(callback.value_or("")));
                          return std::vector<Value>{};
                      });
              },
              {}) {
        files["level95.lua"] = chunkOf(kHub);
        files["level95_chapter6"] = chunkOf(kChapter);
        files["level95_chapter11"] = chunkOf(kChapter);
        files["hub/docks-return.lua"] = chunkOf(kModChapter);
    }

    // Loads the hub: its main chunk, then `plan` applied, then the chapter that Main asked for and its RunLevel.
    void load(const HubChapterPlan& plan) {
        scripts.create();
        REQUIRE(scripts.runFile("level95.lua"));
        applyHubChapter(scripts, plan);
        scripts.servicePreloads(true);
        CHECK(scripts.errors() == 0);
    }

    // fRunMission[row].level; nothing when nil.
    [[nodiscard]] std::optional<double> runMissionLevel(int row) {
        const Value rows = scripts.vm().global("fRunMission");
        REQUIRE(rows.table() != nullptr);
        const Value entry = rows.table()->get(Value(static_cast<double>(row)));
        REQUIRE(entry.table() != nullptr);
        return entry.table()->field("level").number();
    }
};

// The plan for hub chapter `id` at `checkpoint`.
HubChapterPlan planOf(std::string id, int checkpoint, bool original, std::optional<StoryStep> offer,
                      std::string script = {}) {
    return {.id = std::move(id),
            .checkpoint = checkpoint,
            .original = original,
            .mod = original ? "" : "yard-brawl",
            .script = std::move(script),
            .offer = std::move(offer)};
}

} // namespace

TEST_CASE("hub chapter scripts: an original chapter plays its own actions with the offer it already has",
          "[hub_chapter]") {
    // Expected: story-progress.md#hub, confirmed (code)
    Hub hub;
    hub.load(planOf("hub:6", 6, true, StoryStep::mission(86)));
    CHECK(hub.notes == std::vector<std::string>{"scenes", "submission", "1,1,606", "2,2,0", "3,9,0", "crew,7,3"});
    CHECK(hub.runMissionLevel(6) == 86.0);
}

TEST_CASE("hub chapter scripts: an original chapter offering a mod's mission writes its number into fRunMission",
          "[hub_chapter]") {
    // Expected: ../guides/studio/mission-mods.md#graph-natives, Coney's own design (no counterpart in the original)
    Hub hub;
    StoryStep numbered = StoryStep::modMission("yard-brawl.docks");
    numbered.level = 4;
    hub.load(planOf("hub:6", 6, true, numbered));
    CHECK(hub.runMissionLevel(6) == 4.0);
    // Its actions are chapter 6's own.
    CHECK(hub.notes.size() == 6);

    // A mod's mission the session has not numbered (#1300) loads nothing rather than chapter 6's old mission.
    Hub unnumbered;
    unnumbered.load(planOf("hub:6", 6, true, StoryStep::modMission("yard-brawl.docks")));
    CHECK_FALSE(unnumbered.runMissionLevel(6).has_value());
}

TEST_CASE("hub chapter scripts: Coney's default chapter keeps chapter n's crew, a red circle at the giver, no scene",
          "[hub_chapter]") {
    // Expected: ../guides/studio/mission-mods.md#graph-questions (the default chapter, decided 2026-11-01); the red
    // circle then the load is the original's chapter 11 shape, story-progress.md#hub, confirmed (code)
    Hub hub;
    hub.load(planOf("hub:6.yard-brawl.docks", 6, false, StoryStep::mission(86)));
    CHECK(hub.notes == std::vector<std::string>{"1,1,606", "2,9,0", "crew,7,3"});
    CHECK(hub.runMissionLevel(6) == 86.0);
    // The chapter's own callback is back in place, and the default's actions are configured once, as a chapter's are.
    const Value runLevel = hub.scripts.vm().global("RunLevel");
    REQUIRE(runLevel.function() != nullptr);
    CHECK(runLevel.function()->proto != nullptr);
    CHECK(hub.scripts.vm().global("CfgMissionActions").isNil());
}

TEST_CASE("hub chapter scripts: a new chapter after the story's end offers nothing", "[hub_chapter]") {
    // Expected: ../guides/studio/mission-mods.md#anchors (chapters 11 and 12 offer nothing), Coney's own design
    Hub hub;
    hub.checkpoint = 11;
    hub.load(planOf("hub:11.yard-brawl.docks", 11, false, std::nullopt));
    CHECK(hub.notes == std::vector<std::string>{"1,1,0", "2,9,0", "crew,7,3"});
    CHECK_FALSE(hub.runMissionLevel(11).has_value());
}

TEST_CASE("hub chapter scripts: a mod's chapter script runs over the default, in chapter n's time", "[hub_chapter]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own design (no counterpart in the original)
    Hub hub;
    hub.load(planOf("hub:6.yard-brawl.docks", 6, false, StoryStep::mission(86), "hub/docks-return.lua"));
    CHECK(hub.notes == std::vector<std::string>{"1,2,0", "2,9,0", "crew,7,3"});
    CHECK(hub.runMissionLevel(6) == 86.0);
}
