// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lua_vm.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "scripting/lua_value.h"
#include "support/lua_fixtures.h"

using coney::Error;
using coney::ErrorCode;
using coney::script::LuaVm;
using coney::script::LuaVmOptions;
using coney::script::Table;
using coney::script::Value;
using coney::test::luaAB;
using coney::test::luaChunk;
using coney::test::LuaFunctionSpec;
using coney::test::LuaOp;
using coney::test::luaS;
using coney::test::luaU;

namespace {

// Runs `main` in `vm` and returns what it returns.
std::expected<std::vector<Value>, Error> runSpec(LuaVm& vm, const LuaFunctionSpec& main) {
    auto chunk = coney::script::loadLuaChunk(luaChunk(main));
    REQUIRE(chunk.has_value());
    return vm.run(*chunk);
}

// The number in `value`, or a sentinel that fails any comparison with a real result.
double numberOf(const Value& value) { return value.number().value_or(-12345.0); }

} // namespace

TEST_CASE("a script builds tables with SETTABLE, SETLIST and SETMAP and stores them in globals", "[lua_vm]") {
    // T = {}; T.HUD = {}; T.HUD[3] = "three"; L = {"a", "b"}; M = {k = 7}
    LuaFunctionSpec main;
    main.strings = {"T", "HUD", "three", "L", "a", "b", "M", "k"};
    main.code = {
        luaU(LuaOp::CreateTable, 0),  luaU(LuaOp::SetGlobal, 0),    // T = {}
        luaU(LuaOp::GetGlobal, 0),    luaU(LuaOp::PushString, 1),   // T, "HUD"
        luaU(LuaOp::CreateTable, 0),  luaAB(LuaOp::SetTable, 3, 3), // T.HUD = {}
        luaU(LuaOp::GetGlobal, 0),    luaU(LuaOp::GetDotted, 1),    // T.HUD
        luaS(LuaOp::PushInt, 3),      luaU(LuaOp::PushString, 2),   // 3, "three"
        luaAB(LuaOp::SetTable, 3, 3),                               // T.HUD[3] = "three"
        luaU(LuaOp::CreateTable, 2),  luaU(LuaOp::PushString, 4),   // {"a",
        luaU(LuaOp::PushString, 5),   luaAB(LuaOp::SetList, 0, 2),  //  "b"}
        luaU(LuaOp::SetGlobal, 3),                                  // L = ...
        luaU(LuaOp::CreateTable, 1),  luaU(LuaOp::PushString, 7),   // {k =
        luaS(LuaOp::PushInt, 7),      luaU(LuaOp::SetMap, 1),       //  7}
        luaU(LuaOp::SetGlobal, 6),    luaU(LuaOp::End, 0),          // M = ...
    };
    LuaVm vm;
    auto ran = runSpec(vm, main);
    REQUIRE(ran.has_value());
    const std::shared_ptr<Table> hud = vm.global("T").table()->field("HUD").table();
    REQUIRE(hud);
    CHECK(hud->get(Value(3.0)).string() == "three");
    CHECK(hud->size() == 1);
    const std::shared_ptr<Table> list = vm.global("L").table();
    REQUIRE(list);
    CHECK(list->get(Value(1.0)).string() == "a");
    CHECK(list->get(Value(2.0)).string() == "b");
    CHECK(numberOf(vm.global("M").table()->field("k")) == 7.0);
}

TEST_CASE("a script calls a native binding and a Lua function and gets their results", "[lua_vm]") {
    // function add(x) return x + 10 end; R = add(Twice(4)); S = "n=" .. R
    LuaFunctionSpec add;
    add.numParams = 1;
    add.code = {luaU(LuaOp::GetLocal, 0), luaS(LuaOp::PushInt, 10), luaU(LuaOp::Add, 0), luaU(LuaOp::Return, 1)};
    LuaFunctionSpec main;
    main.strings = {"add", "Twice", "R", "S", "n="};
    main.protos = {add};
    main.code = {
        luaAB(LuaOp::Closure, 0, 0), luaU(LuaOp::SetGlobal, 0),                          // add = function
        luaU(LuaOp::GetGlobal, 0),   luaU(LuaOp::GetGlobal, 1), luaS(LuaOp::PushInt, 4), // add, Twice, 4
        luaAB(LuaOp::Call, 1, 1),                                                        // Twice(4), 1 result
        luaAB(LuaOp::Call, 0, 1),    luaU(LuaOp::SetGlobal, 2),                          // R = add(...)
        luaU(LuaOp::PushString, 4),  luaU(LuaOp::GetGlobal, 2), luaU(LuaOp::Concat, 2),  // "n=" .. R
        luaU(LuaOp::SetGlobal, 3),   luaU(LuaOp::End, 0),
    };
    LuaVm vm;
    int calls = 0;
    vm.registerFunction("Twice", [&calls](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
        ++calls;
        return std::vector<Value>{Value(*args[0].number() * 2)};
    });
    auto ran = runSpec(vm, main);
    REQUIRE(ran.has_value());
    CHECK(calls == 1);
    CHECK(numberOf(vm.global("R")) == 18.0);
    CHECK(vm.global("S").string() == "n=18");
}

TEST_CASE("`for k, v in t` visits every entry and a numeric for counts", "[lua_vm]") {
    // t = {5, 6, 7}; for k, v in t do Sum(k, v) end; for i = 1, 3 do Sum(i, 0) end
    LuaFunctionSpec main;
    main.strings = {"Sum"};
    main.code = {
        luaU(LuaOp::CreateTable, 3), luaS(LuaOp::PushInt, 5),   luaS(LuaOp::PushInt, 6),  luaS(LuaOp::PushInt, 7),
        luaAB(LuaOp::SetList, 0, 3),                           // the table (local 0)
        luaU(LuaOp::GetLocal, 0),    luaS(LuaOp::LForPrep, 5), // locals 1-3: table copy, k, v
        luaU(LuaOp::GetGlobal, 0),   luaU(LuaOp::GetLocal, 2),  luaU(LuaOp::GetLocal, 3), luaAB(LuaOp::Call, 4, 0),
        luaS(LuaOp::LForLoop, -5),   luaS(LuaOp::PushInt, 1),   luaS(LuaOp::PushInt, 3),  luaS(LuaOp::PushInt, 1),
        luaS(LuaOp::ForPrep, 5),     luaU(LuaOp::GetGlobal, 0), luaU(LuaOp::GetLocal, 1), luaS(LuaOp::PushInt, 0),
        luaAB(LuaOp::Call, 4, 0),    luaS(LuaOp::ForLoop, -5),  luaU(LuaOp::End, 0),
    };
    LuaVm vm;
    double keys = 0;
    double values = 0;
    int calls = 0;
    vm.registerFunction("Sum", [&](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
        ++calls;
        keys += *args[0].number();
        values += *args[1].number();
        return std::vector<Value>{};
    });
    auto ran = runSpec(vm, main);
    REQUIRE(ran.has_value());
    CHECK(calls == 6);
    CHECK(keys == 12.0); // 1 + 2 + 3 from the table, 1 + 2 + 3 from the counter
    CHECK(values == 18.0);
}

TEST_CASE("a conditional jump follows Lua's comparison", "[lua_vm]") {
    // if P == 2 then A = 1 else A = 0 end; if 1 < 2 then B = 1 end
    LuaFunctionSpec main;
    main.strings = {"P", "A", "B"};
    main.code = {
        luaU(LuaOp::GetGlobal, 0), luaS(LuaOp::PushInt, 2), luaS(LuaOp::JmpNe, 3),   luaS(LuaOp::PushInt, 1),
        luaU(LuaOp::SetGlobal, 1), luaS(LuaOp::Jmp, 2),     luaS(LuaOp::PushInt, 0), luaU(LuaOp::SetGlobal, 1),
        luaS(LuaOp::PushInt, 2),   luaS(LuaOp::PushInt, 1), luaS(LuaOp::JmpLt, 2),   luaS(LuaOp::PushInt, 1),
        luaU(LuaOp::SetGlobal, 2), luaU(LuaOp::End, 0),
    };
    LuaVm vm;
    vm.setGlobal("P", Value(2.0));
    REQUIRE(runSpec(vm, main).has_value());
    CHECK(numberOf(vm.global("A")) == 1.0);
    CHECK(numberOf(vm.global("B")) == 1.0); // 2 < 1 is false, so the jump over B = 1 is not taken
    vm.setGlobal("P", Value());
    REQUIRE(runSpec(vm, main).has_value());
    CHECK(numberOf(vm.global("A")) == 0.0);
}

TEST_CASE("calling nil fails, unless nil calls are no-ops", "[lua_vm]") {
    // Missing(1)
    LuaFunctionSpec main;
    main.strings = {"Missing"};
    main.code = {luaU(LuaOp::GetGlobal, 0), luaS(LuaOp::PushInt, 1), luaAB(LuaOp::Call, 0, 0), luaU(LuaOp::End, 0)};
    LuaVm strict;
    auto failed = runSpec(strict, main);
    REQUIRE(!failed.has_value());
    CHECK(failed.error().code == ErrorCode::Invalid);

    LuaVmOptions lenientOptions;
    lenientOptions.nilCallsAreNoOps = true;
    LuaVm lenient(lenientOptions);
    REQUIRE(runSpec(lenient, main).has_value());
    CHECK(lenient.nilCalls() == 1);
}

TEST_CASE("malformed bytecode and runtime errors fail instead of crashing", "[lua_vm]") {
    LuaVmOptions limited;
    limited.maxInstructions = 1000;
    LuaVm vm(limited);
    // Each of these programs is broken in one way.
    const std::vector<std::vector<std::uint32_t>> programs = {
        {luaU(LuaOp::Pop, 1), luaU(LuaOp::End, 0)},                                // underflow
        {luaU(LuaOp::PushString, 5), luaU(LuaOp::End, 0)},                         // constant out of range
        {luaS(LuaOp::Jmp, 100)},                                                   // jump out of range
        {luaS(LuaOp::PushInt, 1)},                                                 // runs off the end
        {static_cast<std::uint32_t>(60)},                                          // unknown opcode
        {luaS(LuaOp::Jmp, -1)},                                                    // endless loop
        {luaU(LuaOp::PushNil, 1), luaU(LuaOp::GetDotted, 0), luaU(LuaOp::End, 0)}, // index nil
        {luaU(LuaOp::PushNil, 2), luaU(LuaOp::Mult, 0), luaU(LuaOp::End, 0)},      // arithmetic on nil
    };
    for (const auto& code : programs) {
        LuaFunctionSpec main;
        main.strings = {"k"};
        main.code = code;
        auto ran = runSpec(vm, main);
        REQUIRE(!ran.has_value());
        CHECK(ran.error().code == ErrorCode::Invalid);
    }
}

TEST_CASE("table iteration follows insertion order and skips removed entries", "[lua_vm]") {
    Table table;
    REQUIRE(table.set(Value(std::string("b")), Value(1.0)).has_value());
    REQUIRE(table.set(Value(2.0), Value(2.0)).has_value());
    REQUIRE(table.set(Value(std::string("a")), Value(3.0)).has_value());
    REQUIRE(table.set(Value(2.0), Value()).has_value());
    const auto first = table.next(Value());
    REQUIRE(first.has_value());
    if (!first) {
        return;
    }
    CHECK(first->first.string() == "b");
    const auto second = table.next(first->first);
    REQUIRE(second.has_value());
    if (!second) {
        return;
    }
    CHECK(second->first.string() == "a");
    CHECK(!table.next(second->first).has_value());
    CHECK(table.size() == 2);
    CHECK(table.get(Value(-0.0)).isNil());
    REQUIRE(table.set(Value(0.0), Value(4.0)).has_value());
    CHECK(numberOf(table.get(Value(-0.0))) == 4.0);
    CHECK(!table.set(Value(), Value(1.0)).has_value());
}
