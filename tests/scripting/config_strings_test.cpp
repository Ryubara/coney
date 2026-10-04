// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/config_strings.h"

#include <cstddef>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/language.h"
#include "gui/global_strings.h"
#include "support/lua_fixtures.h"

using coney::Error;
using coney::ErrorCode;
using coney::Language;
using coney::gui::GlobalStrings;
using coney::gui::StringTable;
using coney::script::loadGlobalStrings;
using coney::script::ScriptSource;
using coney::test::luaAB;
using coney::test::luaChunk;
using coney::test::LuaFunctionSpec;
using coney::test::LuaOp;
using coney::test::luaS;
using coney::test::luaU;

namespace {

// enum_preload.lua stand-in: E = 1.
LuaFunctionSpec enumScript() {
    LuaFunctionSpec main;
    main.strings = {"E"};
    main.code = {luaS(LuaOp::PushInt, 1), luaU(LuaOp::SetGlobal, 0), luaU(LuaOp::End, 0)};
    return main;
}

// config_preload2.lua stand-in, shaped like the game's string part:
//   X = {"en", "es", "fr", "it", "de"}
//   doFile("config_strings_" .. X[GetLanguage() + 1])
//   for k, v in GSTRING.HUD do CfgHUDMessage(k, v) end
//   CfgChar(1)   -- a binding Coney does not have
LuaFunctionSpec configScript() {
    LuaFunctionSpec main;
    main.strings = {"en",          "es",      "fr",     "it",
                    "de",          "X",       "doFile", "config_strings_",
                    "GetLanguage", "GSTRING", "HUD",    "CfgHUDMessage",
                    "CfgChar"};
    main.code = {
        luaU(LuaOp::CreateTable, 5), luaU(LuaOp::PushString, 0), luaU(LuaOp::PushString, 1),
        luaU(LuaOp::PushString, 2),  luaU(LuaOp::PushString, 3), luaU(LuaOp::PushString, 4),
        luaAB(LuaOp::SetList, 0, 5), luaU(LuaOp::SetGlobal, 5), // X = {...}
        luaU(LuaOp::GetGlobal, 6),   luaU(LuaOp::PushString, 7), luaU(LuaOp::GetGlobal, 5),
        luaU(LuaOp::GetGlobal, 8),   luaAB(LuaOp::Call, 3, 1), // GetLanguage()
        luaS(LuaOp::PushInt, 1),     luaU(LuaOp::Add, 0),        luaU(LuaOp::GetTable, 0),
        luaU(LuaOp::Concat, 2),      luaAB(LuaOp::Call, 0, 0), // doFile(...)
        luaU(LuaOp::GetGlobal, 9),   luaU(LuaOp::GetDotted, 10), luaS(LuaOp::LForPrep, 5),
        luaU(LuaOp::GetGlobal, 11),  luaU(LuaOp::GetLocal, 1),   luaU(LuaOp::GetLocal, 2),
        luaAB(LuaOp::Call, 3, 0),    luaS(LuaOp::LForLoop, -5),                            // CfgHUDMessage(k, v)
        luaU(LuaOp::GetGlobal, 12),  luaS(LuaOp::PushInt, 1),    luaAB(LuaOp::Call, 0, 0), // CfgChar(1)
        luaU(LuaOp::End, 0),
    };
    return main;
}

// config_strings_<code>.lua stand-in: GSTRING = {HUD = {}}; GSTRING.HUD[31] = text.
LuaFunctionSpec stringsScript(const std::string& text) {
    LuaFunctionSpec main;
    main.strings = {"GSTRING", "HUD", text};
    main.code = {
        luaU(LuaOp::CreateTable, 0), luaU(LuaOp::SetGlobal, 0),    luaU(LuaOp::GetGlobal, 0),
        luaU(LuaOp::PushString, 1),  luaU(LuaOp::CreateTable, 0),  luaAB(LuaOp::SetTable, 3, 3),
        luaU(LuaOp::GetGlobal, 0),   luaU(LuaOp::GetDotted, 1),    luaS(LuaOp::PushInt, 31),
        luaU(LuaOp::PushString, 2),  luaAB(LuaOp::SetTable, 3, 3), luaU(LuaOp::End, 0),
    };
    return main;
}

// The scripts by WAD file name. Copying a script can only fail by running out of memory, which ends the test anyway.
ScriptSource sources(std::map<std::string, std::vector<std::byte>> files) {
    // NOLINTNEXTLINE(bugprone-exception-escape): see above
    return [files = std::move(files)](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
        const auto it = files.find(std::string(name));
        if (it == files.end()) {
            return coney::fail(ErrorCode::NotFound, std::string(name));
        }
        return it->second;
    };
}

} // namespace

TEST_CASE("the string load runs the language's strings file and fills the HUD table", "[config_strings]") {
    const ScriptSource source = sources({
        {"enum_preload.lua", luaChunk(enumScript())},
        {"config_preload2.lua", luaChunk(configScript())},
        {"config_strings_en.lua", luaChunk(stringsScript("Select"))},
        {"config_strings_fr.lua", luaChunk(stringsScript("Choisir"))},
    });

    GlobalStrings english;
    auto report = loadGlobalStrings(source, Language::English, english);
    REQUIRE(report.has_value());
    CHECK(english.get(31) == "Select");
    CHECK(english.get(30).empty());
    CHECK(english.size(StringTable::Hud) == 1);
    CHECK(report->skippedCalls == 1); // CfgChar
    CHECK(report->instructions > 0);

    GlobalStrings french;
    REQUIRE(loadGlobalStrings(source, Language::French, french).has_value());
    CHECK(french.get(31) == "Choisir");

    // German has no strings file here: the load fails with the missing file's error.
    GlobalStrings german;
    auto missing = loadGlobalStrings(source, Language::German, german);
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

TEST_CASE("the string bindings refuse a bad id or a non-string text", "[config_strings]") {
    coney::script::LuaVm vm;
    GlobalStrings strings;
    coney::script::addStringBindings(vm, strings);
    using coney::script::Value;
    const Value hud = vm.global("CfgHUDMessage");
    CHECK(vm.call(hud, std::vector<Value>{Value(2.0), Value(std::string("two"))}).has_value());
    CHECK(strings.get(2) == "two");
    CHECK(!vm.call(hud, std::vector<Value>{Value(-1.0), Value(std::string("x"))}).has_value());
    CHECK(!vm.call(hud, std::vector<Value>{Value(1.5), Value(std::string("x"))}).has_value());
    CHECK(!vm.call(hud, std::vector<Value>{Value(3.0), Value(3.0)}).has_value());
    CHECK(vm.call(vm.global("CfgCrimeMessage"), std::vector<Value>{Value(0.0), Value(std::string("c"))}).has_value());
    CHECK(strings.get(StringTable::Crime, 0) == "c");
    CHECK(strings.get(0).empty()); // the crime table is not the HUD table
}
