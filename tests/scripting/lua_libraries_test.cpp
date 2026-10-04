// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's Lua 4.0 base, string and math libraries, called directly as a script would call them
// (docs/research/scripting.md#libraries).
#include "scripting/lua_libraries.h"

#include <cmath>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"

using coney::Error;
using coney::script::LuaVm;
using coney::script::Table;
using coney::script::Value;

namespace {

// A VM with all three libraries.
struct Libraries {
    LuaVm vm;
    std::vector<std::string> printed;

    Libraries() {
        coney::script::BaseLibraryHooks hooks;
        hooks.print = [this](std::string_view line) { printed.emplace_back(line); };
        coney::script::openBaseLibrary(vm, std::move(hooks));
        coney::script::openStringLibrary(vm);
        coney::script::openMathLibrary(vm);
    }

    // Calls the global function `name` with `args`; REQUIREs success.
    std::vector<Value> call(std::string_view name, std::vector<Value> args = {}) {
        auto result = vm.call(vm.global(name), args);
        INFO(std::string(name) << ": " << (result ? std::string() : result.error().message));
        REQUIRE(result.has_value());
        return result ? *result : std::vector<Value>{};
    }

    // The first result of `name(args)` as a string ("" when it is not one).
    std::string text(std::string_view name, std::vector<Value> args) {
        const std::vector<Value> results = call(name, std::move(args));
        return results.empty() ? std::string() : std::string(results[0].string().value_or(""));
    }

    // The first result of `name(args)` as a number (NaN when it is not one).
    double number(std::string_view name, std::vector<Value> args) {
        const std::vector<Value> results = call(name, std::move(args));
        return results.empty() ? std::nan("") : results[0].number().value_or(std::nan(""));
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

// A table holding `items` at 1, 2, ...
std::shared_ptr<Table> list(const std::vector<Value>& items) {
    auto table = std::make_shared<Table>();
    for (std::size_t i = 0; i < items.size(); ++i) {
        REQUIRE(table->set(Value(static_cast<double>(i + 1)), items[i]).has_value());
    }
    return table;
}

} // namespace

TEST_CASE("base library: type, tostring, tonumber and _VERSION", "[lua_libraries]") {
    Libraries lua;
    CHECK(lua.text("type", {Value()}) == "nil");
    CHECK(lua.text("type", {Value(1.0)}) == "number");
    CHECK(lua.text("type", {Value(std::make_shared<Table>())}) == "table");
    CHECK(lua.text("tostring", {Value(1.5)}) == "1.5");
    CHECK(lua.text("tostring", {Value(100.0)}) == "100");
    CHECK(lua.text("tostring", {Value()}) == "nil");
    CHECK(lua.number("tonumber", {str(" 12 ")}) == 12.0);
    CHECK(lua.number("tonumber", {str("ff"), Value(16.0)}) == 255.0);
    CHECK(lua.call("tonumber", {str("x")})[0].isNil());
    CHECK(lua.vm.global("_VERSION").string() == "Lua 4.0.1");
    lua.call("print", {str("a"), Value(2.0)});
    CHECK(lua.printed == std::vector<std::string>{"a\t2"});
    // assert and error fail the call.
    CHECK(!lua.vm.call(lua.vm.global("assert"), std::vector<Value>{Value()}).has_value());
    CHECK(!lua.vm.call(lua.vm.global("error"), std::vector<Value>{str("boom")}).has_value());
}

TEST_CASE("base library: getn, tinsert and tremove keep the field n as Lua 4.0 does", "[lua_libraries]") {
    Libraries lua;
    const auto t = list({Value(1.0), Value(2.0)});
    CHECK(lua.number("getn", {Value(t)}) == 2.0);
    lua.call("tinsert", {Value(t), Value(3.0)});
    CHECK(t->field("n").number() == 3.0);
    lua.call("tinsert", {Value(t), Value(1.0), Value(0.0)}); // at the front
    CHECK(t->get(Value(1.0)).number() == 0.0);
    CHECK(t->get(Value(4.0)).number() == 3.0);
    CHECK(lua.number("tremove", {Value(t), Value(1.0)}) == 0.0);
    CHECK(lua.number("getn", {Value(t)}) == 3.0);
    CHECK(t->get(Value(4.0)).isNil());
}

TEST_CASE("base library: sort, foreach, foreachi and call", "[lua_libraries]") {
    Libraries lua;
    const auto t = list({Value(3.0), Value(1.0), Value(2.0)});
    lua.call("sort", {Value(t)});
    CHECK(t->get(Value(1.0)).number() == 1.0);
    CHECK(t->get(Value(3.0)).number() == 3.0);
    // A comparison that sorts in reverse.
    lua.vm.registerFunction("greater", [](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
        return std::vector<Value>{args[0].number() > args[1].number() ? Value(1.0) : Value()};
    });
    lua.call("sort", {Value(t), lua.vm.global("greater")});
    CHECK(t->get(Value(1.0)).number() == 3.0);
    // foreachi stops at the first result that is not nil.
    lua.vm.registerFunction("second", [](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
        return std::vector<Value>{args[0].number() == 2.0 ? args[1] : Value()};
    });
    CHECK(lua.number("foreachi", {Value(t), lua.vm.global("second")}) == 2.0);
    // call(f, {args}) unpacks the table.
    CHECK(lua.text("call", {lua.vm.global("tostring"), Value(list({Value(7.0)}))}) == "7");
}

TEST_CASE("string library: strsub, strfind with patterns, gsub and format", "[lua_libraries]") {
    Libraries lua;
    CHECK(lua.number("strlen", {str("hello")}) == 5.0);
    CHECK(lua.text("strsub", {str("hello"), Value(2.0), Value(-2.0)}) == "ell");
    CHECK(lua.text("strupper", {str("abc")}) == "ABC");
    CHECK(lua.number("strbyte", {str("A")}) == 65.0);
    CHECK(lua.number("ascii", {str("B")}) == 66.0);
    CHECK(lua.text("strchar", {Value(72.0), Value(105.0)}) == "Hi");
    CHECK(lua.text("strrep", {str("ab"), Value(3.0)}) == "ababab");

    // strfind: plain, a pattern with a capture, an anchor, and no match.
    auto found = lua.call("strfind", {str("level100.lua"), str(".")});
    REQUIRE(found.size() == 2);
    CHECK(found[0].number() == 1.0); // "." is a pattern: any character
    found = lua.call("strfind", {str("level100.lua"), str("."), Value(1.0), Value(1.0)});
    CHECK(found[0].number() == 9.0); // plain: the dot itself
    found = lua.call("strfind", {str("level100"), str("(%a+)(%d+)")});
    REQUIRE(found.size() == 4);
    CHECK(found[2].string() == "level");
    CHECK(found[3].string() == "100");
    CHECK(lua.call("strfind", {str("abc"), str("^b")})[0].isNil());
    found = lua.call("strfind", {str("x = [a[b]c]"), str("%b[]")});
    CHECK(found[0].number() == 5.0);
    CHECK(found[1].number() == 11.0);

    // gsub with references and with a function.
    auto replaced = lua.call("gsub", {str("hello world"), str("(%w+)"), str("<%1>")});
    CHECK(replaced[0].string() == "<hello> <world>");
    CHECK(replaced[1].number() == 2.0);
    replaced = lua.call("gsub", {str("a1b2"), str("%d"), lua.vm.global("tostring"), Value(1.0)});
    CHECK(replaced[0].string() == "a1b2");
    CHECK(replaced[1].number() == 1.0);
    replaced = lua.call("gsub", {str("abc"), str(""), str("-")});
    CHECK(replaced[0].string() == "-a-b-c-");

    // format: the C conversions.
    CHECK(lua.text("format", {str("%d|%5.2f|%-4s|%x|%03d|%s"), Value(7.9), Value(3.14159), str("ab"), Value(255.0),
                              Value(5.0), Value(1.0)}) == "7| 3.14|ab  |ff|005|1");
    CHECK(lua.text("format", {str("%g %q %%"), Value(0.5), str("a\"b")}) == "0.5 \"a\\\"b\" %");
    CHECK(lua.text("strformat", {str("%c"), Value(65.0)}) == "A");
    CHECK(!lua.vm.call(lua.vm.global("format"), std::vector<Value>{str("%y"), Value(1.0)}).has_value());
}

TEST_CASE("math library: degrees, rounding, min and max, and a deterministic random", "[lua_libraries]") {
    Libraries lua;
    CHECK(std::abs(lua.number("sin", {Value(90.0)}) - 1.0) < 1e-12);
    CHECK(std::abs(lua.number("atan2", {Value(1.0), Value(1.0)}) - 45.0) < 1e-12);
    CHECK(lua.number("floor", {Value(-1.5)}) == -2.0);
    CHECK(lua.number("mod", {Value(7.0), Value(3.0)}) == 1.0);
    CHECK(lua.number("max", {Value(1.0), Value(5.0), Value(3.0)}) == 5.0);
    CHECK(lua.number("min", {Value(4.0), Value(-2.0)}) == -2.0);
    CHECK(lua.number("abs", {str("-3")}) == 3.0); // numeric strings are numbers to the library
    CHECK(lua.vm.global("PI").number() > 3.14);
    // random(m, n) stays in range, and the sequence repeats from the same seed.
    std::vector<double> first;
    for (int i = 0; i < 20; ++i) {
        const double r = lua.number("random", {Value(3.0), Value(5.0)});
        CHECK(r >= 3.0);
        CHECK(r <= 5.0);
        first.push_back(r);
    }
    Libraries again;
    for (const double expected : first) {
        CHECK(again.number("random", {Value(3.0), Value(5.0)}) == expected);
    }
}
