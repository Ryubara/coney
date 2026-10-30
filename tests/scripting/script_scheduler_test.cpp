// SPDX-License-Identifier: GPL-3.0-or-later
// The script scheduler: every script run is an entry, in the frame's order (the preloads that arrived, the due
// scheduled calls oldest first, the update function), nested inside the entry that caused it, and each project's time
// per step is recorded with no budget (docs/guides/asset-and-script-architecture.md#script-parts,
// docs/research/scripting.md#scheduled-calls), over synthetic chunks.
#include "scripting/script_scheduler.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "scripting/lua_libraries.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "support/lua_fixtures.h"

using coney::Error;
using coney::ErrorCode;
using coney::script::LuaVm;
using coney::script::ScriptEntry;
using coney::script::ScriptEntryKind;
using coney::script::ScriptSystem;
using coney::script::Value;
using coney::test::LuaAsm;

namespace {

// A script system over in-memory scripts with a binding `Note(text)` that records its text and moves a test clock on
// by 7 ns, and a watcher that writes each entry as `<depth>:<kind> <name>`.
struct Harness {
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<std::string> notes;
    std::vector<std::string> entries;
    std::chrono::nanoseconds clock{};
    ScriptSystem scripts;

    Harness()
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
                  const auto found = files.find(name);
                  if (found == files.end()) {
                      return coney::fail(ErrorCode::NotFound, std::string(name) + ": no such script");
                  }
                  return found->second;
              },
              [this](ScriptSystem& /*system*/, LuaVm& vm) {
                  vm.registerFunction(
                      "Note", [this](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
                          notes.push_back(args.empty() ? std::string{} : coney::script::luaToString(args[0]));
                          clock += std::chrono::nanoseconds(7);
                          return std::vector<Value>{};
                      });
              },
              ScriptSystem::Log{}) {
        scripts.create();
        scripts.scheduler().setClock([this] { return clock; });
        scripts.scheduler().watch([this](const ScriptEntry& entry, int depth) {
            entries.push_back(
                std::format("{}:{} {}", depth, coney::script::scriptEntryKindName(entry.kind), entry.name));
        });
    }

    // Adds a script that calls Note(text).
    void addNote(const std::string& name, const std::string& text) {
        LuaAsm a;
        a.getGlobal("Note").pushString(text).call(1);
        files[name] = coney::test::luaChunk(a.end());
    }
};

} // namespace

TEST_CASE("a frame runs the arrived preloads, the due scheduled calls oldest first, then the update function",
          "[script_scheduler]") {
    // Expected: scripting.md#scheduled-calls, confirmed (code)
    Harness h;
    h.addNote("late.lua", "file");
    h.scripts.setTime(0);
    const std::array<double, 1> first{1.0};
    const std::array<double, 1> second{2.0};
    h.scripts.schedule("Note", 20, second);
    h.scripts.schedule("Note", 10, first);
    h.scripts.preload("late.lua", "Note");
    h.scripts.setUpdateFunction("Note");

    h.scripts.scheduler().run(ScriptEntry::frame(33, 1.0 / 30.0));
    CHECK(h.entries == std::vector<std::string>{"0:frame ", "1:preloads ", "2:file late.lua", "2:call Note",
                                                "1:call Note", "1:call Note", "1:call Note"});
    // The file, its callback, the call due first, the other, then the update function with the step.
    REQUIRE(h.notes.size() == 5);
    CHECK(h.notes[0] == "file");
    CHECK(h.notes[1].empty());
    CHECK(h.notes[2] == "1");
    CHECK(h.notes[3] == "2");
    // The step keeps its fraction: Coney's stand-in until #1130, where the original passes whole milliseconds (33).
    CHECK(h.notes[4].starts_with("33.33"));
}

TEST_CASE("a level entry runs global.lua then the level's script, each a file inside it", "[script_scheduler]") {
    // Expected: scripting.md#life-of-the-lua-state, confirmed (code)
    Harness h;
    h.addNote("global.lua", "global");
    h.addNote("level9.lua", "level");
    h.scripts.scheduler().run(ScriptEntry::levelEntry("level9"));
    CHECK(h.entries == std::vector<std::string>{"0:level entry level9", "1:file global.lua", "1:file level9.lua"});
    CHECK(h.notes == std::vector<std::string>{"global", "level"});

    // The preload lists the same way: a file at a time, in order.
    h.entries.clear();
    const std::array<std::string_view, 2> lists{"global.lua", "level9.lua"};
    h.scripts.scheduler().run(ScriptEntry::fileList(lists));
    CHECK(h.entries == std::vector<std::string>{"0:files ", "1:file global.lua", "1:file level9.lua"});
}

TEST_CASE("a call from C++ is an entry, whichever door it comes through, and gives its results", "[script_scheduler]") {
    // Expected: unverified, #1118
    Harness h;
    const std::array<Value, 1> args{Value(std::string("hit"))};
    CHECK(h.scripts.scheduler().run(ScriptEntry::message("Note", args)).has_value());
    CHECK(h.scripts.scheduler().run(ScriptEntry::animCallback("Note", args)).has_value());
    CHECK(h.scripts.call("Note", args));
    CHECK(!h.scripts.scheduler().run(ScriptEntry::call("Missing")).has_value());
    CHECK(h.entries ==
          std::vector<std::string>{"0:message Note", "0:anim callback Note", "0:call Note", "0:call Missing"});
    CHECK(h.notes == std::vector<std::string>{"hit", "hit", "hit"});
    CHECK(!h.scripts.scheduler().running());
}

TEST_CASE("each project's script time is recorded per step, with no budget", "[script_scheduler]") {
    // Expected: unverified, #1118
    Harness h;
    CHECK(h.scripts.scheduler().projectTime() == nullptr);
    const std::array<Value, 1> args{Value(std::string("hit"))};
    // A message in the step, then the frame with its update function: 7 ns each.
    h.scripts.scheduler().run(ScriptEntry::message("Note", args));
    h.scripts.setUpdateFunction("Note");
    h.scripts.scheduler().run(ScriptEntry::frame(33, 1.0 / 30.0));
    const coney::script::ProjectTime* game = h.scripts.scheduler().projectTime(coney::script::kGameProject);
    REQUIRE(game != nullptr);
    CHECK(game->steps == 1);
    CHECK(game->lastStep == std::chrono::nanoseconds(14));
    CHECK(game->thisStep == std::chrono::nanoseconds(0));
    // A step whose frame runs nothing took no time.
    h.scripts.setUpdateFunction("");
    h.scripts.scheduler().run(ScriptEntry::frame(66, 1.0 / 30.0));
    CHECK(game->steps == 2);
    CHECK(game->lastStep == std::chrono::nanoseconds(0));
}
