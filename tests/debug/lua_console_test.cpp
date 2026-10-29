// SPDX-License-Identifier: GPL-3.0-or-later
// The debug menus' Lua console on Coney's Lua 4.0 compiler (docs/guides/debug-menu.md#lua-console): lines and files
// compiled and run in the sandbox's script state, values printed, errors naming their lines.
#include "debug/lua_console.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/native_caller.h"

using coney::debug::LuaConsole;
using coney::debug::SandboxScripts;
using coney::script::Value;

TEST_CASE("the console assigns globals, calls bindings and prints values", "[debug]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred
    SandboxScripts sandbox({});
    auto& vm = sandbox.scripts().vm();
    LuaConsole console;
    // A local in the line: the console compiles Lua 4.0, so a line holds what a chunk can.
    REQUIRE(console.run(vm, "local one = 1 SCENETEST = one"));
    CHECK(vm.global("SCENETEST") == Value(1.0));
    // An expression prints its values; `=` in front of one does too, as in Lua's own console.
    CHECK(console.run(vm, "ToInt(7.9)") == std::vector<std::string>{"= 7"});
    CHECK(console.run(vm, "=GetPlatform() + 1") == std::vector<std::string>{"= 2"});
    REQUIRE(console.run(vm, "t = {1, 2; name = 'x'}; t.extra = t[2] * 10"));
    CHECK(console.run(vm, "= t.extra, t.name") == std::vector<std::string>{"= 20, \"x\""});
    CHECK(console.run(vm, "= nil or 'b', 1 and nil, not nil, 2 < 3, 'a' .. 1") ==
          std::vector<std::string>{"= \"b\", nil, 1, 1, \"a1\""});
    // Short circuit: the right side of `and` is not run when the left is nil.
    REQUIRE(console.run(vm, "x = nil and ObjSpawn()"));
    CHECK(console.run(vm, "=ObjSpawn()") == std::vector<std::string>{"= 1"});
    CHECK(console.history()->size() == 8);
}

TEST_CASE("the console runs statements, locals, loops and functions", "[debug]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred
    // The console compiles each line as Lua 4.0 (docs/guides/debug-menu.md#lua-console): a loop runs in it.
    SandboxScripts sandbox({});
    auto& vm = sandbox.scripts().vm();
    LuaConsole console;
    REQUIRE(console.run(vm, "local n = 0 for i = 1, 10 do if i ~= 5 then n = n + i end end TOTAL = n"));
    CHECK(vm.global("TOTAL") == Value(50.0));
    REQUIRE(console.run(vm, "function Twice(x) local y = x * 2 return y end"));
    CHECK(console.run(vm, "Twice(21)") == std::vector<std::string>{"= 42"});
    CHECK(console.run(vm, "local s = '' local i = 0 while i < 3 do i = i + 1 s = s .. i end return s") ==
          std::vector<std::string>{"= \"123\""});
    // A statement with no values prints nothing.
    CHECK(console.run(vm, "repeat TOTAL = TOTAL - 20 until TOTAL < 0") == std::vector<std::string>{});
    CHECK(vm.global("TOTAL") == Value(-10.0));
}

TEST_CASE("the console reports syntax and runtime errors with their lines", "[debug]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred
    SandboxScripts sandbox({});
    auto& vm = sandbox.scripts().vm();
    LuaConsole console;
    const auto syntax = console.run(vm, "x = (1");
    REQUIRE_FALSE(syntax);
    CHECK(syntax.error().message.starts_with("console:1:"));
    const auto later = console.run(vm, "local a = 1\nif a then\nnothing.field = 1\nend");
    REQUIRE_FALSE(later);
    CHECK(later.error().message.find("console:3:") != std::string::npos);
    // The sandbox skips calls of unset globals (bindings Coney lacks), so the failing call is of a number.
    CHECK_FALSE(console.run(vm, "local n = 5 n()"));
    CHECK_FALSE(console.run(vm, "'unfinished"));
    // Statements before a runtime error have run.
    CHECK_FALSE(console.run(vm, "BEFORE = 1 local n = 5 n()"));
    CHECK(vm.global("BEFORE") == Value(1.0));
}

TEST_CASE("the console runs a file of Lua source and prints what it returns", "[debug]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred
    SandboxScripts sandbox({});
    auto& vm = sandbox.scripts().vm();
    LuaConsole console;
    const auto path = std::filesystem::temp_directory_path() / "coney-console-test.txt";
    {
        std::ofstream file(path);
        file << "-- a comment\na = 2\nlocal b = 3\nreturn a * b\n";
    }
    const auto printed = console.runFile(vm, path.string());
    std::filesystem::remove(path);
    REQUIRE(printed);
    CHECK(*printed == std::vector<std::string>{"= 6"});
    CHECK_FALSE(console.runFile(vm, path.string()));
}
