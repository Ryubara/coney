// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's chunk writer (writeLuaChunk), read back by its reader: the stripped form `luac -s` writes.
#include <cstddef>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scripting/lua_chunk.h"
#include "support/lua_fixtures.h"

using coney::script::parseLuaChunk;
using coney::test::luaChunk;
using coney::test::LuaFunctionSpec;
using coney::test::LuaOp;
using coney::test::luaU;

TEST_CASE("a written chunk reads back to the same functions, stripped as luac -s strips one", "[lua_chunk]") {
    // Expected: formats/lua-bytecode.md#what-a-chunk-keeps-and-what-stripping-removes, inferred
    LuaFunctionSpec nested;
    nested.numParams = 1;
    nested.vararg = true;
    nested.strings = {"inner"};
    nested.code = {luaU(LuaOp::PushString, 0), luaU(LuaOp::End, 0)};
    LuaFunctionSpec main;
    main.strings = {"GSTRING", "", std::string("a\0b", 3)};
    main.numbers = {0.5, -3.0, 1e300};
    main.protos = {nested};
    main.code = {luaU(LuaOp::PushString, 0), luaU(LuaOp::PushNum, 2), luaU(LuaOp::End, 0)};
    auto original = parseLuaChunk(luaChunk(main));
    REQUIRE(original.has_value());
    original->protos[0].lineDefined = 7;
    original->lines = {1, 2, 3};

    const std::vector<std::byte> written = coney::script::writeLuaChunk(*original);
    auto again = parseLuaChunk(written);
    REQUIRE(again.has_value());
    // Stripped: the source names and the line table go, the line each function was defined on stays.
    CHECK(again->source == "=(none)");
    CHECK(again->lines.empty());
    CHECK(again->strings == original->strings);
    CHECK(again->numbers == original->numbers);
    CHECK(again->code == original->code);
    CHECK(again->maxStackSize == original->maxStackSize);
    REQUIRE(again->protos.size() == 1);
    CHECK(again->protos[0].source == "=(none)");
    CHECK(again->protos[0].lineDefined == 7);
    CHECK(again->protos[0].numParams == 1);
    CHECK(again->protos[0].isVararg);
    CHECK(again->protos[0].strings == std::vector<std::string>{"inner"});
    CHECK(again->protos[0].code == original->protos[0].code);
    // Written twice, the bytes are the same.
    CHECK(coney::script::writeLuaChunk(*again) == written);
}
