// SPDX-License-Identifier: GPL-3.0-or-later
// The script system: the Lua state's life (made, destroyed and made again), scripts run by name, the level entry, calls
// by dotted name and the schedule of delayed calls (docs/research/scripting.md), over synthetic chunks.
#include "scripting/script_system.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "scripting/lua_libraries.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "support/lua_fixtures.h"

using coney::Error;
using coney::ErrorCode;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;
using coney::test::LuaAsm;

namespace {

// A script system over in-memory scripts, with a binding `Note(...)` that records each call's arguments, joined by
// commas.
struct Harness {
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<std::string> requested;
    std::vector<std::string> notes;
    std::vector<std::string> log;
    ScriptSystem scripts;

    Harness()
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
                  requested.emplace_back(name);
                  const auto found = files.find(name);
                  if (found == files.end()) {
                      return coney::fail(ErrorCode::NotFound, std::string(name) + ": no such script");
                  }
                  return found->second;
              },
              [this](ScriptSystem& /*system*/, LuaVm& vm) {
                  vm.registerFunction("Note",
                                      [this](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
                                          std::string note;
                                          for (std::size_t i = 0; i < args.size(); ++i) {
                                              note += (i == 0 ? "" : ",") + coney::script::luaToString(args[i]);
                                          }
                                          notes.push_back(note);
                                          return std::vector<Value>{};
                                      });
              },
              [this](std::string_view line) { log.emplace_back(line); }) {}

    // Adds a script file.
    void add(const std::string& name, const coney::test::LuaFunctionSpec& main) {
        files[name] = coney::test::luaChunk(main);
    }

    // Whether a log line contains `text`.
    [[nodiscard]] bool logged(std::string_view text) const {
        for (const std::string& line : log) {
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

// A script that calls Note(text).
coney::test::LuaFunctionSpec noteScript(const std::string& text) {
    LuaAsm a;
    a.getGlobal("Note").pushString(text).call(1);
    return a.end();
}

// A script that defines `T = {}` and `T.<name> = function(self?, x) Note(<text>, x) end`; the function's first
// parameter is `x`, or `self` then `x` for a method.
coney::test::LuaFunctionSpec tableFunctionScript(const std::string& name, const std::string& text, bool method) {
    LuaAsm body;
    body.spec.numParams = method ? 2 : 1;
    body.getGlobal("Note").pushString(text);
    // GETLOCAL of the last parameter, x.
    body.spec.code.push_back(coney::test::luaU(coney::test::LuaOp::GetLocal, method ? 1 : 0));
    // The local pushed above is not counted by the assembler; call with both arguments by hand.
    body.spec.code.push_back(
        coney::test::luaAB(coney::test::LuaOp::Call, static_cast<std::uint32_t>(body.spec.numParams), 0));
    body.spec.code.push_back(coney::test::luaU(coney::test::LuaOp::End, 0));
    LuaAsm main;
    main.spec.protos.push_back(body.spec);
    main.newTable().setGlobal("T");
    main.getGlobal("T").pushString(name).closure(0).setTable();
    return main.end();
}

} // namespace

TEST_CASE("a fresh state has the libraries, the bindings and silent error handlers", "[script_system]") {
    Harness h;
    CHECK_FALSE(h.scripts.exists());
    h.scripts.create();
    REQUIRE(h.scripts.exists());
    CHECK(h.scripts.generation() == 1);
    LuaVm& vm = h.scripts.vm();
    for (const char* name : {"strfind", "tinsert", "floor", "Note", "_ERRORMESSAGE", "_ALERT"}) {
        INFO(name);
        CHECK(vm.global(name).function() != nullptr);
    }
    // The error handlers do nothing and return nothing.
    auto alerted = vm.call(vm.global("_ALERT"), std::vector<Value>{Value(std::string("x"))});
    REQUIRE(alerted.has_value());
    CHECK(alerted->empty());
}

TEST_CASE("destroying and making the state again starts from the bindings alone", "[script_system]") {
    Harness h;
    LuaAsm set;
    set.pushInt(5).setGlobal("Kept");
    h.add("set.lua", set.end());
    h.scripts.create();
    REQUIRE(h.scripts.runFile("set.lua"));
    CHECK(h.scripts.vm().global("Kept").number() == 5.0);
    h.scripts.schedule("Note", 100);
    h.scripts.setUpdateFunction("Note");
    // The level unload: destroy, then create.
    h.scripts.destroy();
    CHECK_FALSE(h.scripts.exists());
    h.scripts.create();
    CHECK(h.scripts.generation() == 2);
    CHECK(h.scripts.vm().global("Kept").isNil());
    CHECK(h.scripts.vm().global("Note").function() != nullptr);
    CHECK(h.scripts.scheduled() == 0);
    CHECK(h.scripts.updateFunction().empty());
}

TEST_CASE("the level entry runs global.lua, then the level's script, in one state", "[script_system]") {
    Harness h;
    LuaAsm global;
    global.pushInt(1).setGlobal("G");
    h.add("global.lua", global.end());
    LuaAsm level;
    level.getGlobal("G").pushInt(1).add().setGlobal("L");
    h.add("level100.lua", level.end());
    h.scripts.create();
    h.scripts.enterLevel("level100");
    CHECK(h.requested == std::vector<std::string>{"global.lua", "level100.lua"});
    CHECK(h.scripts.vm().global("L").number() == 2.0);
    CHECK(h.scripts.errors() == 0);
}

TEST_CASE("a missing or failing script is logged and counted, and the next one still runs", "[script_system]") {
    Harness h;
    LuaAsm broken; // indexes nil: a runtime error
    broken.getGlobal("Nothing").getField("x");
    h.add("broken.lua", broken.end());
    h.add("fine.lua", noteScript("fine"));
    h.scripts.create();
    const std::array<std::string_view, 3> names{"absent.lua", "broken.lua", "fine.lua"};
    h.scripts.runFiles(names);
    CHECK(h.scripts.errors() == 2);
    CHECK(h.logged("script error: absent.lua"));
    CHECK(h.logged("script error: broken.lua"));
    CHECK(h.notes == std::vector<std::string>{"fine"});
}

TEST_CASE("a call of a binding Coney lacks is skipped and logged once by name", "[script_system]") {
    Harness h;
    LuaAsm a;
    a.getGlobal("NoSuchBinding").pushInt(1).call(1);
    a.getGlobal("NoSuchBinding").pushInt(2).call(1);
    a.getGlobal("Note").pushString("after").call(1);
    h.add("calls.lua", a.end());
    h.scripts.create();
    REQUIRE(h.scripts.runFile("calls.lua"));
    CHECK(h.notes == std::vector<std::string>{"after"});
    CHECK(h.scripts.skippedCalls() == 2);
    CHECK(h.logged("`NoSuchBinding`"));
    std::size_t lines = 0;
    for (const std::string& line : h.log) {
        lines += line.find("NoSuchBinding") != std::string::npos ? 1 : 0;
    }
    CHECK(lines == 1);
}

TEST_CASE("functions are called by dotted name, and a colon passes the table as self", "[script_system]") {
    Harness h;
    h.add("plain.lua", tableFunctionScript("f", "plain", false));
    h.scripts.create();
    REQUIRE(h.scripts.runFile("plain.lua"));
    const std::array<Value, 1> args{Value(7.0)};
    CHECK(h.scripts.call("T.f", args));
    CHECK(h.notes == std::vector<std::string>{"plain,7"});

    h.add("method.lua", tableFunctionScript("m", "method", true));
    REQUIRE(h.scripts.runFile("method.lua"));
    CHECK(h.scripts.call("T:m", args));
    CHECK(h.notes.back() == "method,7");

    // A name that does not resolve is not called, and is not a script error.
    CHECK_FALSE(h.scripts.call("T.missing"));
    CHECK_FALSE(h.scripts.call("Nope.f"));
    CHECK(h.scripts.errors() == 0);
}

TEST_CASE("scheduled calls run when due, in due order, ties in the order scheduled", "[script_system]") {
    Harness h;
    h.scripts.create();
    h.scripts.setTime(1000);
    const std::array<double, 1> one{1.0};
    const std::array<double, 1> two{2.0};
    const std::array<double, 1> three{3.0};
    // Note,1 at 1500; Note,2 at 1200; Note,3 at 1500 (after Note,1); a name with no function at 1100.
    h.scripts.schedule("Note", 500, one);
    h.scripts.schedule("Note", 200, two);
    h.scripts.schedule("Note", 500, three);
    h.scripts.schedule("Missing.f", 100);
    CHECK(h.scripts.scheduled() == 4);
    h.scripts.update(1199, 0.0);
    CHECK(h.notes.empty());
    CHECK(h.scripts.scheduled() == 3); // the unresolved name was dropped silently
    h.scripts.update(1200, 0.0);
    CHECK(h.notes == std::vector<std::string>{"2"});
    h.scripts.update(2000, 0.0);
    CHECK(h.notes == std::vector<std::string>{"2", "1", "3"});
    CHECK(h.scripts.errors() == 0);
}

TEST_CASE("a call scheduled during an update waits for the next one; flushing drops calls", "[script_system]") {
    Harness h;
    // Again() schedules Note after 0 ms.
    h.scripts.create();
    h.scripts.vm().registerFunction("Again", [&h](std::span<const Value>) -> std::expected<std::vector<Value>, Error> {
        h.scripts.schedule("Note", 0);
        return std::vector<Value>{};
    });
    h.scripts.schedule("Again", 0);
    h.scripts.update(0, 0.0);
    CHECK(h.notes.empty());
    CHECK(h.scripts.scheduled() == 1);
    h.scripts.update(33, 0.0);
    CHECK(h.notes.size() == 1);

    h.scripts.schedule("Note", 10);
    h.scripts.schedule("Again", 10);
    h.scripts.flushScheduled("Note");
    CHECK(h.scripts.scheduled() == 1);
    h.scripts.flushScheduled();
    CHECK(h.scripts.scheduled() == 0);
}

TEST_CASE("the update function gets the step in milliseconds every frame", "[script_system]") {
    Harness h;
    h.scripts.create();
    h.scripts.setUpdateFunction("Note");
    h.scripts.update(33, 1.0 / 30.0);
    // Note's argument is the step, 1/30 s in milliseconds.
    REQUIRE(h.notes.size() == 1);
    CHECK(h.notes[0].starts_with("33.33"));
}

TEST_CASE("the call trace shows the calls into the scripts and every binding call with its arguments",
          "[script_system]") {
    Harness h;
    h.add("plain.lua", tableFunctionScript("f", "plain", false));
    h.scripts.create();
    std::vector<std::string> trace;
    h.scripts.traceCalls([&trace](std::string_view line) { trace.emplace_back(line); });
    REQUIRE(h.scripts.runFile("plain.lua"));
    const std::array<Value, 1> args{Value(7.5)};
    CHECK(h.scripts.call("T.f", args));
    CHECK(trace == std::vector<std::string>{"> T.f(7.5)\n", "Note(\"plain\", 7.5)\n"});

    // A short list of numbers is shown whole; the trace lasts into the next state.
    trace.clear();
    h.scripts.create();
    auto position = std::make_shared<coney::script::Table>();
    double key = 1.0;
    for (const double v : {1.0, 2.5, -3.0}) {
        REQUIRE(position->set(Value(key), Value(v)));
        key += 1.0;
    }
    const std::array<Value, 2> noteArgs{Value(position), Value()};
    CHECK(h.scripts.call("Note", noteArgs));
    CHECK(trace == std::vector<std::string>{"> Note({1, 2.5, -3}, nil)\n", "Note({1, 2.5, -3}, nil)\n"});

    // Stopped: nothing more.
    trace.clear();
    h.scripts.traceCalls({});
    CHECK(h.scripts.call("Note", noteArgs));
    CHECK(trace.empty());
    CHECK(h.notes.size() == 3);
}

TEST_CASE("a preloaded file runs at the next update, then its callback; one it asks for waits", "[script_system]") {
    Harness h;
    // a.lua asks for b.lua (Chain) and notes "a"; b.lua notes "b".
    LuaAsm a;
    a.getGlobal("Chain").call(0).getGlobal("Note").pushString("a").call(1);
    h.add("a.lua", a.end());
    h.add("b.lua", noteScript("b"));
    h.scripts.create();
    h.scripts.vm().registerFunction("Chain", [&h](std::span<const Value>) -> std::expected<std::vector<Value>, Error> {
        h.scripts.preload("b.lua", "");
        return std::vector<Value>{};
    });

    // The caller goes on: nothing runs until the file arrives.
    h.scripts.preload("a.lua", "Note");
    CHECK(h.notes.empty());
    CHECK(h.scripts.preloadsPending() == 1);
    // It arrives at the next update: the chunk, then the callback with no arguments.
    h.scripts.update(33, 0.0);
    CHECK(h.notes == std::vector<std::string>{"a", ""});
    CHECK(h.scripts.preloadsPending() == 1);
    h.scripts.update(66, 0.0);
    CHECK(h.notes == std::vector<std::string>{"a", "", "b"});

    // The level start's preload runs everything, chained files too; a missing file calls no callback.
    h.notes.clear();
    h.scripts.preload("a.lua", "");
    h.scripts.preload("missing.lua", "Note");
    h.scripts.servicePreloads(true);
    CHECK(h.notes == std::vector<std::string>{"a", "b"});
    CHECK(h.scripts.preloadsPending() == 0);
}
