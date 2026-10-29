// SPDX-License-Identifier: GPL-3.0-or-later
// The native caller: the generated signature table, argument editing with defaults, and calls of a real binding, a
// stub and a missing one through the script VM. The Lua console's tests are in lua_console_test.cpp.
#include "debug/native_caller.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scripting/script_bindings.h"

using coney::debug::NativeArgType;
using coney::debug::NativeArguments;
using coney::debug::NativeStatus;
using coney::debug::SandboxScripts;
using coney::script::Value;

TEST_CASE("the signature table lists all 956 bindings, once each, with their arguments", "[debug]") {
    // Expected: scripting.md#bindings, confirmed (code)
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
    // Expected: docs/guides/debug-menu.md#natives
    CHECK(coney::debug::nativeStatus("GetPlatform") == NativeStatus::Implemented);
    CHECK(coney::debug::nativeStatus("PlayMovie") == NativeStatus::Partial);
    CHECK(coney::debug::nativeStatus("SetDifficulty") == NativeStatus::Stub);
    CHECK(coney::debug::nativeStatus("GoalPathBlocker") == NativeStatus::Missing);
}

TEST_CASE("arguments start at their defaults and become the values a script would pass", "[debug]") {
    // Expected: docs/guides/debug-menu.md#natives
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
    // Expected: docs/guides/debug-menu.md#natives
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
    // Expected: docs/guides/debug-menu.md#natives
    CHECK(coney::debug::formatValue(Value()) == "nil");
    CHECK(coney::debug::formatValue(Value(2.5)) == "2.5");
    CHECK(coney::debug::formatValue(Value(std::string("x"))) == "\"x\"");
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(4.0)).has_value());
    CHECK(coney::debug::formatValue(Value(table)) == "{1=4}");
}
