// SPDX-License-Identifier: GPL-3.0-or-later
// The native caller: the generated signature table, argument editing with defaults, calls of a real binding, a stub
// and a missing one through the script VM, and the Lua console over the same VM.
#include "debug/native_caller.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/lua_console.h"
#include "scripting/script_bindings.h"

using coney::debug::LuaConsole;
using coney::debug::NativeArgType;
using coney::debug::NativeArguments;
using coney::debug::NativeStatus;
using coney::debug::SandboxScripts;
using coney::script::Value;

TEST_CASE("the signature table lists all 956 bindings, once each, with their arguments", "[debug]") {
    const auto all = coney::debug::nativeSignatures();
    CHECK(all.size() == 956);
    std::set<std::string_view> names;
    for (const auto& signature : all) {
        names.insert(signature.name);
    }
    CHECK(names.size() == all.size());
    // Every binding Coney registers is in the masterlist, so the menus can call it.
    for (const auto& info : coney::script::bindingTable()) {
        CHECK(names.contains(info.name));
    }
    const auto* brDead = coney::debug::findNativeSignature("BrDead");
    REQUIRE(brDead != nullptr);
    CHECK(brDead->category == "ai");
    REQUIRE(brDead->args.size() == 2);
    CHECK(brDead->args[0].type == NativeArgType::Handle);
    CHECK(brDead->args[1].type == NativeArgType::Boolean);
    CHECK(brDead->args[1].defaultValue == "true");
    CHECK(coney::debug::findNativeSignature("NoSuchBinding") == nullptr);
}

TEST_CASE("the status comes from Coney's binding table", "[debug]") {
    CHECK(coney::debug::nativeStatus("GetPlatform") == NativeStatus::Implemented);
    CHECK(coney::debug::nativeStatus("PlayMovie") == NativeStatus::Partial);
    CHECK(coney::debug::nativeStatus("SetDifficulty") == NativeStatus::Stub);
    CHECK(coney::debug::nativeStatus("GoalPathBlocker") == NativeStatus::Missing);
}

TEST_CASE("arguments start at their defaults and become the values a script would pass", "[debug]") {
    NativeArguments args(*coney::debug::findNativeSignature("BrDead"));
    CHECK(args.values()[0].number == 0.0);
    CHECK(args.values()[1].flag);
    args.values()[0].number = 7;
    CHECK(args.callText() == "BrDead(7, 1)");
    args.values()[1].flag = false;
    const std::vector<Value> values = args.toValues();
    REQUIRE(values.size() == 2);
    CHECK(values[0] == Value(7.0));
    CHECK(values[1].isNil()); // false is nil in Lua 4.0
    args.values()[1].omitted = true;
    CHECK(args.toValues().size() == 1);
}

TEST_CASE("a call goes through the VM: a real binding answers, a stub records, a missing one fails", "[debug]") {
    std::vector<std::string> log;
    SandboxScripts sandbox([&log](std::string_view line) { log.emplace_back(line); });
    auto& vm = sandbox.scripts().vm();

    NativeArguments toInt(*coney::debug::findNativeSignature("ToInt"));
    toInt.values()[0].number = 3.75;
    const auto values = toInt.toValues();
    const auto truncated = coney::debug::callNative(vm, "ToInt", values);
    REQUIRE(truncated);
    REQUIRE(truncated->size() == 1);
    CHECK((*truncated)[0] == Value(3.0));

    const auto stubbed = coney::debug::callNative(vm, "SetDifficulty", {});
    REQUIRE(stubbed);
    CHECK(stubbed->empty()); // a stub with no result

    const std::vector<Value> objArgs{Value(std::string("crate")), Value(2.0)};
    REQUIRE(coney::debug::callNative(vm, "CfgObj", objArgs));
    CHECK(sandbox.recorded().count("CfgObj") == 1);

    const auto missing = coney::debug::callNative(vm, "GoalPathBlocker", {});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == coney::ErrorCode::NotFound);

    // A routed binding reaches the sandbox's host.
    const std::vector<Value> movie{Value(std::string("intro"))};
    REQUIRE(coney::debug::callNative(vm, "PlayMovie", movie));
    CHECK(std::ranges::find(log, "sandbox: PlayMovie(intro)") != log.end());
}

TEST_CASE("values are shown as a script writer reads them", "[debug]") {
    CHECK(coney::debug::formatValue(Value()) == "nil");
    CHECK(coney::debug::formatValue(Value(2.5)) == "2.5");
    CHECK(coney::debug::formatValue(Value(std::string("x"))) == "\"x\"");
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(4.0)).has_value());
    CHECK(coney::debug::formatValue(Value(table)) == "{1=4}");
}

TEST_CASE("the console assigns globals, calls bindings and prints values", "[debug]") {
    SandboxScripts sandbox({});
    auto& vm = sandbox.scripts().vm();
    LuaConsole console;
    REQUIRE(console.run(vm, "SCENETEST = 1"));
    CHECK(vm.global("SCENETEST") == Value(1.0));
    const auto printed = console.run(vm, "ToInt(7.9); =GetPlatform() + 1");
    REQUIRE(printed);
    CHECK(*printed == std::vector<std::string>{"= 7", "= 2"});
    REQUIRE(console.run(vm, "t = {1, 2, name = 'x'}; t.extra = t[2] * 10"));
    CHECK(console.run(vm, "= t.extra, t.name") == std::vector<std::string>{"= 20, \"x\""});
    CHECK(console.run(vm, "= nil or 'b', 1 and nil, not nil, 2 < 3, 'a' .. 1") ==
          std::vector<std::string>{"= \"b\", nil, 1, 1, \"a1\""});
    // Short circuit: the right side of `and` is not run when the left is nil.
    REQUIRE(console.run(vm, "x = nil and ObjSpawn()"));
    CHECK(console.run(vm, "=ObjSpawn()") == std::vector<std::string>{"= 1"});
    CHECK(console.history()->size() == 7);
}

TEST_CASE("the console reports syntax and runtime errors", "[debug]") {
    SandboxScripts sandbox({});
    auto& vm = sandbox.scripts().vm();
    LuaConsole console;
    const auto syntax = console.run(vm, "x = (1");
    REQUIRE_FALSE(syntax);
    CHECK(syntax.error().message.find("column") != std::string::npos);
    CHECK_FALSE(console.run(vm, "nothing.field = 1"));
    CHECK_FALSE(console.run(vm, "NotAFunction()"));
    CHECK_FALSE(console.run(vm, "'unfinished"));
}

TEST_CASE("the console runs a file of lines", "[debug]") {
    SandboxScripts sandbox({});
    auto& vm = sandbox.scripts().vm();
    LuaConsole console;
    const auto path = std::filesystem::temp_directory_path() / "coney-console-test.txt";
    {
        std::ofstream file(path);
        file << "-- a comment\na = 2\n=a * 3\n";
    }
    const auto printed = console.runFile(vm, path.string());
    std::filesystem::remove(path);
    REQUIRE(printed);
    CHECK(*printed == std::vector<std::string>{"= 6"});
    CHECK_FALSE(console.runFile(vm, path.string()));
}
