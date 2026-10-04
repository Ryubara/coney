// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lua_chunk.h"

#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/lua_fixtures.h"

using coney::ErrorCode;
using coney::script::parseLuaChunk;
using coney::test::luaChunk;
using coney::test::LuaFunctionSpec;
using coney::test::LuaOp;
using coney::test::luaU;

TEST_CASE("a Lua 4.0 chunk reads its constants, nested functions and code", "[lua_chunk]") {
    LuaFunctionSpec nested;
    nested.numParams = 2;
    nested.vararg = true;
    nested.code = {luaU(LuaOp::End, 0)};
    LuaFunctionSpec main;
    main.strings = {"GSTRING", ""};
    main.numbers = {0.5, -3.0};
    main.protos = {nested};
    main.code = {luaU(LuaOp::PushString, 0), luaU(LuaOp::End, 0)};

    auto parsed = parseLuaChunk(luaChunk(main));
    REQUIRE(parsed.has_value());
    CHECK(parsed->source == "=(test)");
    CHECK(parsed->strings == std::vector<std::string>{"GSTRING", ""});
    CHECK(parsed->numbers == std::vector<double>{0.5, -3.0});
    CHECK(parsed->code == main.code);
    REQUIRE(parsed->protos.size() == 1);
    CHECK(parsed->protos[0].numParams == 2);
    CHECK(parsed->protos[0].isVararg);
    CHECK(parsed->protos[0].code.size() == 1);
}

TEST_CASE("a chunk with another header or layout is refused", "[lua_chunk]") {
    LuaFunctionSpec main;
    main.code = {luaU(LuaOp::End, 0)};
    const std::vector<std::byte> good = luaChunk(main);

    // Each of these bytes is part of the signature, version or layout: change one and the chunk is not ours.
    for (const std::size_t offset : {std::size_t{0}, std::size_t{4}, std::size_t{5}, std::size_t{6}, std::size_t{12}}) {
        std::vector<std::byte> bad = good;
        bad[offset] = static_cast<std::byte>(0x7f);
        auto parsed = parseLuaChunk(bad);
        REQUIRE(!parsed.has_value());
        CHECK(parsed.error().code == ErrorCode::Invalid);
    }
    // A different test number means another floating-point format.
    std::vector<std::byte> badNumber = good;
    badNumber[13] = static_cast<std::byte>(0x00);
    auto parsed = parseLuaChunk(badNumber);
    REQUIRE(!parsed.has_value());
    CHECK(parsed.error().code == ErrorCode::Invalid);
}

TEST_CASE("a truncated chunk or one with bytes after it is refused", "[lua_chunk]") {
    LuaFunctionSpec main;
    main.strings = {"abc"};
    main.code = {luaU(LuaOp::End, 0)};
    const std::vector<std::byte> good = luaChunk(main);

    // Every cut before the end fails as truncated (or, inside the header, as not a chunk).
    for (std::size_t size = 0; size < good.size(); ++size) {
        auto parsed = parseLuaChunk(std::span(good).first(size));
        REQUIRE(!parsed.has_value());
    }
    auto cut = parseLuaChunk(std::span(good).first(good.size() - 2));
    REQUIRE(!cut.has_value());
    CHECK(cut.error().code == ErrorCode::Truncated);

    std::vector<std::byte> longer = good;
    longer.push_back(std::byte{0});
    auto parsed = parseLuaChunk(longer);
    REQUIRE(!parsed.has_value());
    CHECK(parsed.error().code == ErrorCode::Invalid);
}

TEST_CASE("a count larger than the data fails before allocating", "[lua_chunk]") {
    LuaFunctionSpec main;
    main.code = {luaU(LuaOp::End, 0)};
    std::vector<std::byte> data = luaChunk(main);
    // The code count is the last word before the single instruction: claim a million instructions.
    const std::size_t countAt = data.size() - 8;
    data[countAt + 2] = static_cast<std::byte>(0x0f);
    auto parsed = parseLuaChunk(data);
    REQUIRE(!parsed.has_value());
    CHECK(parsed.error().code == ErrorCode::Truncated);
}
