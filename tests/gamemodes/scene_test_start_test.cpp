// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic scripts make entry ordering observable without any game script source or bytecode.
#include <cstddef>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gamemodes/scene_test_start.h"
#include "scripting/binding_args.h"
#include "support/lua_fixtures.h"

namespace {

// A resetting global fixture, a level observer and a start callback expose all three entry phases.
struct EntryHarness {
    coney::GameState state;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<std::string> reads;
    std::vector<std::string> observed;
    int addedFlags = 0;
    coney::script::ScriptSystem scripts;

    // Read only our two fixtures; all callbacks record Coney-owned test values.
    EntryHarness()
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
                  reads.emplace_back(name);
                  const auto found = files.find(name);
                  if (found == files.end()) {
                      return coney::fail(coney::ErrorCode::NotFound, "no such synthetic script");
                  }
                  return found->second;
              },
              [this](coney::script::ScriptSystem&, coney::script::LuaVm& vm) {
                  vm.registerFunction("ObserveLevel",
                                      [this](std::span<const coney::script::Value>) -> coney::script::binding::Results {
                                          observed.push_back("level:" + flag());
                                          state.startGameCallback = "ObserveStart";
                                          return std::vector<coney::script::Value>{};
                                      });
                  vm.registerFunction("ObserveStart",
                                      [this](std::span<const coney::script::Value>) -> coney::script::binding::Results {
                                          observed.push_back("start:" + flag());
                                          return std::vector<coney::script::Value>{};
                                      });
                  vm.registerFunction("AddFlag",
                                      [this](std::span<const coney::script::Value>) -> coney::script::binding::Results {
                                          ++addedFlags;
                                          return std::vector<coney::script::Value>{};
                                      });
              },
              {}) {
        coney::test::LuaAsm global;
        global.pushString("reset-by-fixture").setGlobal("SCENETEST");
        files["global.lua"] = coney::test::luaChunk(global.end());
        coney::test::LuaAsm level;
        level.getGlobal("ObserveLevel").call(0);
        files["synthetic.lua"] = coney::test::luaChunk(level.end());
        state.checkPoint = 3;
        scripts.create();
    }

    // Snapshot a string while the level or start callback runs.
    std::string flag() { return std::string(scripts.vm().global("SCENETEST").string().value_or("<unset>")); }
};

} // namespace

TEST_CASE("scene-test entry injects its name after global and before level startup", "[scene-test][scripts][debug]") {
    // Expected: unverified, #283 (Coney-only entry ordering and reuse of the level's startup lifecycle).
    EntryHarness h;
    const auto start =
        coney::runSceneTestLevelScript(h.scripts, h.state, h.humans, h.flags, "synthetic", "chosen_scene");
    REQUIRE(start.has_value());
    const std::vector<std::string> expected{"level:chosen_scene", "start:chosen_scene"};
    CHECK(h.observed == expected);
    CHECK(h.flag() == "chosen_scene");
    CHECK(h.addedFlags == 2);
    CHECK(h.state.startGameCallback.empty());
    CHECK(start->level == "synthetic");
    CHECK(start->checkpoint == 3);
    CHECK(h.scripts.errors() == 0);
}

TEST_CASE("scene-test entry leaves ordinary level entry's global reset intact", "[scene-test][scripts][debug]") {
    // Expected: unverified, #283 (ordinary entry must retain its existing global-before-level semantics).
    EntryHarness h;
    h.scripts.vm().setGlobal("SCENETEST", coney::script::Value(std::string("set-too-early")));
    const auto start = coney::runLevelScript(h.scripts, h.state, h.humans, h.flags, "synthetic");
    const std::vector<std::string> expected{"level:reset-by-fixture", "start:reset-by-fixture"};
    CHECK(h.observed == expected);
    CHECK(start.checkpoint == 3);
    CHECK(h.scripts.errors() == 0);
}

TEST_CASE("scene-test entry rejects empty names before touching scripts", "[scene-test][scripts][debug]") {
    // Expected: unverified, #283 (invalid debug input changes no state and starts no load).
    EntryHarness h;
    h.scripts.vm().setGlobal("SCENETEST", coney::script::Value(std::string("before")));
    h.state.startGameCallback = "unchanged";
    std::string_view level = "synthetic";
    std::string_view scene = "chosen_scene";
    SECTION("empty level") { level = {}; }
    SECTION("empty scene") { scene = {}; }
    const auto start = coney::runSceneTestLevelScript(h.scripts, h.state, h.humans, h.flags, level, scene);
    REQUIRE_FALSE(start.has_value());
    CHECK(start.error().code == coney::ErrorCode::InvalidArgument);
    CHECK(h.reads.empty());
    CHECK(h.flag() == "before");
    CHECK(h.state.startGameCallback == "unchanged");
}

TEST_CASE("scene-test entry is cleared by a later ordinary level load", "[scene-test][scripts][debug]") {
    // Expected: unverified, #283 (scene-test state must not carry into ordinary play).
    EntryHarness h;
    REQUIRE(
        coney::runSceneTestLevelScript(h.scripts, h.state, h.humans, h.flags, "synthetic", "chosen_scene").has_value());
    h.observed.clear();
    h.reads.clear();
    const auto start = coney::runLevelScript(h.scripts, h.state, h.humans, h.flags, "synthetic");
    const std::vector<std::string> expected{"level:reset-by-fixture", "start:reset-by-fixture"};
    const std::vector<std::string> reads{"global.lua", "synthetic.lua"};
    CHECK(h.observed == expected);
    CHECK(h.reads == reads);
    CHECK(start.checkpoint == 3);
}

TEST_CASE("scene-test entry reports script failures without leaving debug state", "[scene-test][scripts][debug]") {
    // Expected: unverified, #283 (a failed debug load must not retain its scene or start callback).
    EntryHarness h;
    SECTION("missing global") { h.files.erase("global.lua"); }
    SECTION("missing level") { h.files.erase("synthetic.lua"); }
    const auto start =
        coney::runSceneTestLevelScript(h.scripts, h.state, h.humans, h.flags, "synthetic", "chosen_scene");
    REQUIRE_FALSE(start.has_value());
    CHECK(start.error().code == coney::ErrorCode::Invalid);
    CHECK(h.observed.empty());
    CHECK(h.flag() == "<unset>");
    CHECK(h.state.startGameCallback.empty());
}
