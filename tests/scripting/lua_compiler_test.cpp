// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's Lua 4.0 compiler on synthetic sources of our own: programs compiled and run in Coney's VM against their
// expected results, every instruction kind emitted, luac's own code for the decompiler's fixtures, line information
// and errors. The rules come from the Lua 4.0 reference manual and the code patterns from
// docs/research/formats/lua-bytecode.md#patterns.
#include "scripting/lua_compiler.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "scripting/lua_chunk.h"
#include "scripting/lua_libraries.h"
#include "scripting/lua_opcodes.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "support/test_paths.h"

using coney::Error;
using coney::script::compileLua;
using coney::script::LuaOpcode;
using coney::script::LuaProto;
using coney::script::LuaVm;
using coney::script::Value;

namespace {

// Compiles `source` and runs it in a fresh VM that has `f` (returns its arguments) and `count` (returns how often it
// was called); returns what the chunk returns.
std::expected<std::vector<Value>, Error> run(std::string_view source) {
    auto chunk = coney::script::loadLuaSource(source, "=test");
    if (!chunk) {
        return std::unexpected(chunk.error());
    }
    LuaVm vm;
    auto calls = std::make_shared<double>(0.0);
    vm.registerFunction("f", [](std::span<const Value> args) { return std::vector<Value>(args.begin(), args.end()); });
    vm.registerFunction("count", [calls](std::span<const Value>) {
        *calls += 1;
        return std::vector<Value>{Value(*calls)};
    });
    return vm.run(*chunk);
}

// The number values of `results`, with nil as -999 so a comparison shows it.
std::vector<double> numbers(const std::vector<Value>& results) {
    std::vector<double> out;
    out.reserve(results.size());
    for (const Value& value : results) {
        out.push_back(value.isNil() ? -999.0 : value.number().value_or(-12345.0));
    }
    return out;
}

// Runs `source`, REQUIREs success and returns its results as numbers.
std::vector<double> runNumbers(std::string_view source) {
    auto results = run(source);
    if (!results) {
        FAIL(results.error().message);
    }
    return numbers(*results);
}

// The one string `source` returns.
std::string runString(std::string_view source) {
    auto results = run(source);
    if (!results) {
        FAIL(results.error().message);
    }
    REQUIRE(results->size() == 1);
    return std::string((*results)[0].string().value_or("<not a string>"));
}

// Adds every opcode in `proto` and its nested functions to `seen`.
void collectOpcodes(const LuaProto& proto, std::set<LuaOpcode>& seen) {
    for (const std::uint32_t word : proto.code) {
        seen.insert(coney::script::luaOpcodeOf(word));
    }
    for (const LuaProto& nested : proto.protos) {
        collectOpcodes(nested, seen);
    }
}

// The first way `ours` differs from `theirs` (what `coney-tools lua decompile --roundtrip` compares: parameters, vararg
// flag, stack size, code, constants and nested functions, recursively), or empty when they are the same.
std::string firstDifference(const LuaProto& ours, const LuaProto& theirs, const std::string& path) {
    if (ours.numParams != theirs.numParams || ours.isVararg != theirs.isVararg) {
        return path + ": parameters";
    }
    if (ours.maxStackSize != theirs.maxStackSize) {
        return path + ": stack " + std::to_string(ours.maxStackSize) + " vs " + std::to_string(theirs.maxStackSize);
    }
    for (std::size_t pc = 0; pc < ours.code.size() && pc < theirs.code.size(); ++pc) {
        if (ours.code[pc] != theirs.code[pc]) {
            return path + ": pc " + std::to_string(pc) + ": opcode " +
                   std::to_string(static_cast<int>(coney::script::luaOpcodeOf(ours.code[pc]))) + " vs " +
                   std::to_string(static_cast<int>(coney::script::luaOpcodeOf(theirs.code[pc])));
        }
    }
    if (ours.code.size() != theirs.code.size()) {
        return path + ": " + std::to_string(ours.code.size()) + " vs " + std::to_string(theirs.code.size()) +
               " instructions";
    }
    if (ours.strings != theirs.strings) {
        return path + ": strings";
    }
    if (ours.numbers != theirs.numbers) {
        return path + ": numbers";
    }
    if (ours.protos.size() != theirs.protos.size()) {
        return path + ": nested functions";
    }
    for (std::size_t i = 0; i < ours.protos.size(); ++i) {
        if (auto found = firstDifference(ours.protos[i], theirs.protos[i], path + "/" + std::to_string(i));
            !found.empty()) {
            return found;
        }
    }
    return {};
}

// The bytes of the file at `path`; empty when it cannot be read.
std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The message of the error compiling `source` gives; FAILs when it compiles.
std::string compileError(std::string_view source) {
    auto compiled = compileLua(source, "=test");
    if (compiled) {
        FAIL("compiled: " << source);
    }
    return compiled.error().message;
}

} // namespace

TEST_CASE("compiled Lua 4.0 source gives its expected results", "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred (the values follow the Lua 4.0 reference manual)
    SECTION("arithmetic, with the manual's priorities and right-associative ^") {
        CHECK(runNumbers("return 1 + 2 * 3, (1 + 2) * 3, 2 ^ 3 ^ 2, -2 ^ 2, 7 - 2 - 1, 8 / 2 / 2") ==
              std::vector<double>{7, 9, 512, -4, 4, 2});
        CHECK(runNumbers("local x = 5 return x + 1, x - 1, 1 - x, x - -2, - -x, -1.5, 1e2, .5, 3.") ==
              std::vector<double>{6, 4, -4, 7, 5, -1.5, 100, 0.5, 3});
        CHECK(runNumbers("return 33554431 + 1, 33554432, -33554433, 123456789012") ==
              std::vector<double>{33554432, 33554432, -33554433, 123456789012});
    }
    SECTION("strings, escapes, long strings and concatenation") {
        // Lua: return "a" .. 'b' .. 1 .. "\n\t\\\"\65" .. [[x<new line>[[y]] z]]
        CHECK(runString("return \"a\" .. 'b' .. 1 .. \"\\n\\t\\\\\\\"\\65\" .. [[x\n[[y]] z]]") ==
              "ab1\n\t\\\"Ax\n[[y]] z");
        CHECK(runString("local s = [[\nfirst]] return s") == "first");
        CHECK(runString("return (\"a\" .. \"b\") .. \"c\" .. 2 + 3") == "abc5");
    }
    SECTION("comparisons, and, or and not, as values and as conditions") {
        CHECK(runNumbers("return 1 < 2, 2 <= 1, 3 > 2, 3 >= 4, 1 == 1, 1 ~= 1") ==
              std::vector<double>{1, -999, 1, -999, 1, -999});
        CHECK(runNumbers("local a, b = nil, 2 return a and b, a or b, b and 3, not a, not b, a or b and 4") ==
              std::vector<double>{-999, 2, 3, 1, -999, 4});
        CHECK(runNumbers("local a, b = 1, nil return a < 2 or b, (a == 1) and 7, not (a and b), b == nil, a ~= nil") ==
              std::vector<double>{1, 7, 1, 1, 1});
        CHECK(runNumbers("local n = 0 if nil then n = 1 elseif 1 then n = 2 else n = 3 end return n") ==
              std::vector<double>{2});
        CHECK(
            runNumbers("local a, b = 1, 2 local r = 0 if not (a > b) and (a == 1 or b == 9) then r = 1 end return r") ==
            std::vector<double>{1});
    }
    SECTION("locals, globals and multiple assignment") {
        CHECK(runNumbers("local a, b, c = 1, 2 a, b = b, a return a, b, c") == std::vector<double>{2, 1, -999});
        CHECK(runNumbers("x, y = f(1, 2, 3) return x, y") == std::vector<double>{1, 2});
        CHECK(runNumbers("t = {} t.a, t[2], z = 10, 20, 30, 40 return t.a, t[2], z") ==
              std::vector<double>{10, 20, 30});
        CHECK(runNumbers("local x = 1 do local x = 2 end local y = x return x, y") == std::vector<double>{1, 1});
        CHECK(runNumbers("local a = f(1, 2), f(3) return a") == std::vector<double>{1});
    }
    SECTION("tables: lists, records, nesting and long lists flushed every 62 items") {
        CHECK(runNumbers("local t = {10, 20; x = 1, ['y'] = 2, [5] = 50} return t[1], t[2], t.x, t.y, t[5]") ==
              std::vector<double>{10, 20, 1, 2, 50});
        std::string big = "local t = {";
        for (int i = 1; i <= 130; ++i) {
            big += std::to_string(i) + ",";
        }
        big += "} local r = {";
        for (int i = 1; i <= 40; ++i) {
            big += "k" + std::to_string(i) + " = " + std::to_string(i) + ",";
        }
        big += "} return t[1], t[62], t[63], t[124], t[130], r.k1, r.k31, r.k32, r.k40";
        CHECK(runNumbers(big) == std::vector<double>{1, 62, 63, 124, 130, 1, 31, 32, 40});
        CHECK(runNumbers("local t = {{1, {2}}, {a = {b = 3}}} return t[1][2][1], t[2].a.b") ==
              std::vector<double>{2, 3});
        CHECK(runNumbers("local t = {f(1, 2)} return t[1], t[2]") == std::vector<double>{1, -999});
    }
    SECTION("while, repeat, both for loops and break") {
        CHECK(runNumbers("local n, i = 0, 0 while i < 10 do i = i + 1 n = n + i end return n") ==
              std::vector<double>{55});
        CHECK(runNumbers("local i = 0 repeat local j = 1 i = i + j until i >= 4 return i") == std::vector<double>{4});
        CHECK(runNumbers("local n = 0 for i = 1, 10 do n = n + i end for i = 10, 1, -3 do n = n + i end return n") ==
              std::vector<double>{55 + 10 + 7 + 4 + 1});
        CHECK(runNumbers("local n = 0 for i = 1, 100 do local k = i if k > 3 then break end n = n + k end return n") ==
              std::vector<double>{6});
        CHECK(runNumbers("local n = 0 while 1 do n = n + 1 if n == 5 then break end end return n") ==
              std::vector<double>{5});
        CHECK(runNumbers("local s = 0 for k, v in {5, 6, 7} do s = s + k * v end return s") ==
              std::vector<double>{5 + 12 + 21});
        CHECK(runNumbers("local n = 0 for i = 1, 3 do for j = 1, 3 do if j > i then break end n = n + 1 end end "
                         "return n") == std::vector<double>{6});
    }
    SECTION("functions: calls, results, recursion, varargs, methods and tail calls") {
        CHECK(runNumbers("function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end return fact(6)") ==
              std::vector<double>{720});
        CHECK(runNumbers("local function_value = function(a, b) return b, a end return function_value(1, 2)") ==
              std::vector<double>{2, 1});
        CHECK(runNumbers("function v(a, ...) return a, arg.n, arg[2] end return v(1, 2, 3)") ==
              std::vector<double>{1, 2, 3});
        CHECK(runNumbers("o = {n = 5} function o:get(k) return self.n + k end function o.twice(x) return 2 * x end "
                         "return o:get(1), o.twice(4)") == std::vector<double>{6, 8});
        CHECK(runNumbers("function g() return f(4, 5) end return g()") == std::vector<double>{4, 5});
        CHECK(runNumbers("local a = 1 return f(a, f(2, 3))") == std::vector<double>{1, 2, 3});
        CHECK(runNumbers("return count(), count(), (count())") == std::vector<double>{1, 2, 3});
        CHECK(runNumbers("local t = {} function t.m(x) return x end local s = f'7' return t.m{1}[1], s == '7'") ==
              std::vector<double>{1, 1});
    }
    SECTION("closures over upvalues, frozen when the closure is made") {
        CHECK(runNumbers("function make(n) local k = n * 2 return function(x) return x + %k + %n end end "
                         "local add = make(1) return add(10)") == std::vector<double>{13});
        CHECK(runNumbers("g = 1 local h = function() return %g end g = 2 return h(), g") == std::vector<double>{1, 2});
    }
}

TEST_CASE("the compiler emits every Lua 4.0 instruction", "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#the-instruction-set, inferred
    // Each line brings in some instructions; together they use all 49 (the list in lua_opcodes.h).
    const std::string_view source = R"(
local a, b = 1, 2.5
local c
a = -b + -1.5 + a * b / 2 - b ^ 2 - a
x = a .. b .. "s"
t = {1, 2; k = 3}
t.k = t.k + t[a] + t[1]
local u = t[a + 1]
y = not a
if a == b then f() elseif a ~= b then f() end
if a < b or a <= b then f() end
if a > b and a >= b then f() end
z = a and b
w = a or b
v = a < b
while a do f() a = nil end
for i = 1, 3 do local j = i break end
for k, v in t do f(k, v) end
q = a >= b
if not c then f() end
function m(p, ...) return f(%a, %b) end
function r() return 1 end
t:m()
return m(1)
)";
    auto compiled = compileLua(source, "=test");
    if (!compiled) {
        FAIL(compiled.error().message);
    }
    std::set<LuaOpcode> seen;
    collectOpcodes(*compiled, seen);
    // Every opcode from END (0) to CLOSURE (48).
    for (int op = 0; op < coney::script::kLuaOpcodeCount; ++op) {
        INFO("opcode " << op);
        CHECK(seen.contains(static_cast<LuaOpcode>(op)));
    }
}

TEST_CASE("the compiler gives luac's code for the decompiler's fixtures", "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#round-trip, inferred
    // Each fixture is a source of our own with the chunk a stock Lua 4.0.1 `luac -s` made of it
    // (python/tests/lua4/fixtures, docs/research/formats/lua-bytecode.md#round-trip): the same instructions,
    // constants, parameters, stack sizes and nested functions mean the code follows luac's patterns.
    for (const std::string_view name : {"assign", "closures", "control", "expressions", "logic"}) {
        INFO(name);
        const std::string source = readFile(coney::test::lua4FixturesDir() / (std::string(name) + ".lua"));
        const std::string chunk = readFile(coney::test::lua4FixturesDir() / (std::string(name) + ".s.luac"));
        REQUIRE(!source.empty());
        REQUIRE(!chunk.empty());
        auto theirs = coney::script::parseLuaChunk(std::as_bytes(std::span(chunk)));
        REQUIRE(theirs.has_value());
        auto ours = compileLua(source, "@fixture.lua");
        if (!ours) {
            FAIL(ours.error().message);
        }
        CHECK(firstDifference(*ours, *theirs, "main") == "");
    }
}

TEST_CASE("compiled code carries the line of every instruction", "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred
    const std::string_view source = "x = 1\n\nfunction g(a)\n  local b = a\n  return b.field\nend\n";
    auto compiled = compileLua(source, "=lines");
    REQUIRE(compiled.has_value());
    REQUIRE(compiled->lines.size() == compiled->code.size());
    CHECK(compiled->lineDefined == 0);
    CHECK(compiled->lines.front() == 1);
    REQUIRE(compiled->protos.size() == 1);
    const LuaProto& g = compiled->protos.front();
    CHECK(g.lineDefined == 3);
    REQUIRE(g.lines.size() == g.code.size());
    CHECK(g.lines.front() == 4);
    CHECK(g.lines.back() == 6); // END, on the line of `end`

    // A runtime error in compiled code names its chunk and line.
    auto loaded = coney::script::loadLuaSource(source, "=lines");
    REQUIRE(loaded.has_value());
    LuaVm vm;
    REQUIRE(vm.run(*loaded).has_value());
    auto failed = vm.call(vm.global("g"), std::vector<Value>{Value(1.0)});
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message.find("lines:5:") != std::string::npos);
}

TEST_CASE("compile errors name the chunk, the line and the token", "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred
    CHECK(compileError("x = 1\ny = = 2") == "test:2: unexpected symbol near '='");
    CHECK(compileError("x = 'abc\ny = 1").starts_with("test:1: unfinished string"));
    CHECK(compileError("if x then\nf()\n").starts_with("test:3: 'end' expected (to close 'if' at line 1)"));
    CHECK(compileError("break").starts_with("test:1: no loop to break"));
    CHECK(compileError("return 1\nx = 2").starts_with("test:2: '<eof>' expected"));
    CHECK(compileError("function a.b.c() end").starts_with("test:1: '(' expected"));
    CHECK(compileError("x = %y").starts_with("test:1: cannot use an upvalue in the main chunk"));
    CHECK(compileError("local a\nfunction g() return a end").starts_with("test:2: cannot reach local 'a'"));
    CHECK(compileError("function g(a) return %a end").starts_with("test:1: upvalue 'a' must be global or a local of"));
    // Lua 4.0 keeps a constructor's list and record parts apart, joined by `;`.
    CHECK(compileError("t = {1, x = 2}").starts_with("test:1: '}' expected"));
    CHECK(compileError("x = 1e").starts_with("test:1: malformed number"));
    CHECK(compileError("x = 3..2").starts_with("test:1: ambiguous syntax"));
    CHECK(coney::script::luaChunkLabel("@scripts/a.lua") == "scripts/a.lua");
    CHECK(coney::script::luaChunkLabel("x = 1\ny = 2") == "[string \"x = 1...\"]");
}

TEST_CASE("source nested too deeply is a compile error, not a crash", "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred (the limits are Coney's choice; Lua 4.0.1 has none)
    constexpr std::size_t kDeep = 20000;
    const std::string parens = "x = " + std::string(kDeep, '(') + "1" + std::string(kDeep, ')');
    CHECK(compileError(parens).starts_with("test:1: chunk has too many syntax levels"));
    std::string nots = "x = ";
    for (std::size_t i = 0; i < kDeep; ++i) {
        nots += "not ";
    }
    CHECK(compileError(nots + "1").starts_with("test:1: chunk has too many syntax levels"));
    std::string blocks;
    for (std::size_t i = 0; i < kDeep; ++i) {
        blocks += "do ";
    }
    for (std::size_t i = 0; i < kDeep; ++i) {
        blocks += "end ";
    }
    CHECK(compileError(blocks).starts_with("test:1: chunk has too many syntax levels"));
    // Functions nest no deeper than a chunk may: what the compiler accepts, parseLuaChunk() reads back.
    std::string functions;
    for (int i = 0; i <= coney::script::kMaxLuaNesting; ++i) {
        functions += "function f() ";
    }
    for (int i = 0; i <= coney::script::kMaxLuaNesting; ++i) {
        functions += "end ";
    }
    CHECK(compileError(functions).starts_with("test:1: functions nested too deeply"));
    // The deepest nesting that is allowed still compiles.
    std::string allowed = "x = " + std::string(100, '(') + "1" + std::string(100, ')');
    CHECK(compileLua(allowed, "test").has_value());
}

TEST_CASE("base library: dostring compiles and runs Lua source, and gives nil for text that does not compile",
          "[lua_compiler]") {
    // Expected: formats/lua-bytecode.md#compiler, inferred
    // Lua 4.0's `dostring` takes source text as well as a precompiled chunk, telling them apart by the chunk
    // signature's first byte (docs/guides/studio/lua-sdk.md#compiler).
    LuaVm vm;
    coney::script::openBaseLibrary(vm, {});
    const auto call = [&vm](const std::string& text) {
        auto results = vm.call(vm.global("dostring"), std::vector<Value>{Value(text)});
        REQUIRE(results.has_value());
        return *results;
    };
    const std::vector<Value> results = call("local n = 0 for i = 1, 4 do n = n + i end DONE = n return n, 'x'");
    REQUIRE(results.size() == 2);
    CHECK(results[0] == Value(10.0));
    CHECK(results[1] == Value(std::string("x")));
    CHECK(vm.global("DONE") == Value(10.0));
    // A syntax error runs nothing and returns nil, as an unloadable chunk does.
    const std::vector<Value> refused = call("DONE = = 1");
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].isNil());
    CHECK(vm.global("DONE") == Value(10.0));
}
