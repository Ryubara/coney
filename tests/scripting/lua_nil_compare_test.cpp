// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's Lua 4.0 compiler on comparisons with nil: the code must be the code a stock Lua 4.0.1 `luac` writes, since
// edited disc scripts are compiled by Coney (docs/guides/studio/mission-mods.md#compiling). The expected listings are
// those `luac -l` gives of the same sources of our own; the rule is `luaK_code`'s in lcode.c
// (docs/research/formats/lua-bytecode.md#merges).
#include <cstdint>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scripting/lua_compiler.h"
#include "scripting/lua_opcodes.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"

using coney::script::LuaOpcode;

namespace {

// The opcodes of the main function `source` compiles to.
std::vector<LuaOpcode> opcodes(std::string_view source) {
    auto compiled = coney::script::compileLua(source, "=test");
    REQUIRE(compiled.has_value());
    std::vector<LuaOpcode> ops;
    for (const std::uint32_t word : compiled->code) {
        ops.push_back(coney::script::luaOpcodeOf(word));
    }
    return ops;
}

// The one value the chunk `source` returns, as a number (nil as -999).
double runOne(std::string_view source) {
    auto chunk = coney::script::loadLuaSource(source, "=test");
    REQUIRE(chunk.has_value());
    coney::script::LuaVm vm;
    auto results = vm.run(*chunk);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 1);
    const coney::script::Value& value = results->front();
    return value.isNil() ? -999.0 : value.number().value_or(-12345.0);
}

} // namespace

TEST_CASE("the compiler turns a comparison with nil into luac's NOT", "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#merges, inferred (luac -l listings of these sources)
    using enum LuaOpcode;
    SECTION("x == nil as a value is NOT, with the nil's slot still counted") {
        CHECK(opcodes("y = x == nil") == std::vector{GetGlobal, Not, SetGlobal, End});
        auto compiled = coney::script::compileLua("y = x == nil", "=test");
        REQUIRE(compiled.has_value());
        CHECK(compiled->maxStackSize == 2);
        CHECK(opcodes("local a = 1 local b = a == nil return b") ==
              std::vector{PushInt, GetLocal, Not, GetLocal, Return, End});
    }
    SECTION("under not, NOT twice; as a test, NOT then JMPT") {
        CHECK(opcodes("y = not (x == nil)") == std::vector{GetGlobal, Not, Not, SetGlobal, End});
        CHECK(opcodes("if not (x == nil) then f() end") == std::vector{GetGlobal, Not, JmpT, GetGlobal, Call, End});
    }
    SECTION("as a plain test, and x ~= nil, are unchanged") {
        CHECK(opcodes("if x == nil then f() end") == std::vector{GetGlobal, JmpT, GetGlobal, Call, End});
        CHECK(opcodes("y = x ~= nil") == std::vector{GetGlobal, JmpT, PushNilJmp, PushInt, SetGlobal, End});
    }
    SECTION("x ~= nil inside and, or and not stays a plain test: it has no value of its own to keep") {
        CHECK(opcodes("y = (x ~= nil) or b") ==
              std::vector{GetGlobal, JmpT, GetGlobal, Jmp, Pop, PushNilJmp, PushInt, SetGlobal, End});
        CHECK(opcodes("y = (x ~= nil) and b") ==
              std::vector{GetGlobal, JmpF, GetGlobal, Jmp, Pop, PushNilJmp, PushInt, SetGlobal, End});
        CHECK(opcodes("y = not (x ~= nil)") == std::vector{GetGlobal, JmpF, PushNilJmp, PushInt, SetGlobal, End});
        CHECK(opcodes("y = b or x ~= nil") ==
              std::vector{GetGlobal, JmpOnT, GetGlobal, JmpT, PushNilJmp, PushInt, SetGlobal, End});
        CHECK(opcodes("y = (x == nil) or b") ==
              std::vector{GetGlobal, JmpF, GetGlobal, Jmp, Pop, PushNilJmp, PushInt, SetGlobal, End});
    }
    SECTION("the values are Lua's") {
        CHECK(runOne("local a return a == nil") == 1);
        CHECK(runOne("local a = 0 return a == nil") == -999);
        CHECK(runOne("local a return not (a == nil)") == -999);
        CHECK(runOne("local a = 0 return not (a == nil)") == 1);
        CHECK(runOne("local a, b return (a ~= nil) or b") == -999);
        CHECK(runOne("local a, b = 0, nil return (a ~= nil) or b") == 1);
    }
}
