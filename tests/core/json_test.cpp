// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/json.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

using coney::ErrorCode;
using coney::json::parse;

TEST_CASE("JSON objects, arrays and scalars are read", "[json]") {
    const auto doc = parse(R"( {"name": "Coney", "size": 4274269343, "ratio": 0.5, "ok": true, "none": null,
                               "files": [1, "two", {"three": 3}], "empty": {}, "list": []} )");
    REQUIRE(doc.has_value());
    REQUIRE(doc->find("name") != nullptr);
    CHECK(*doc->find("name")->asString() == "Coney");
    CHECK(doc->find("size")->asInteger() == 4274269343);
    CHECK(doc->find("ratio")->asDouble() == 0.5);
    CHECK_FALSE(doc->find("ratio")->asInteger().has_value());
    CHECK(doc->find("ok")->asBool() == true);
    CHECK(doc->find("none")->isNull());
    const auto* files = doc->find("files")->asArray();
    REQUIRE(files != nullptr);
    REQUIRE(files->size() == 3);
    CHECK((*files)[2].find("three")->asInteger() == 3);
    CHECK(doc->find("empty")->asObject()->empty());
    CHECK(doc->find("list")->asArray()->empty());
    CHECK(doc->find("missing") == nullptr);
    CHECK(doc->find("name")->asArray() == nullptr);
}

TEST_CASE("JSON strings decode their escapes and surrogate pairs as UTF-8", "[json]") {
    const auto doc = parse(R"(["a\"b\\c\/d\n\t", "\u00e9", "\ud83d\ude00", "C:\\Games\\Coney"])");
    REQUIRE(doc.has_value());
    const auto& items = *doc->asArray();
    CHECK(*items[0].asString() == "a\"b\\c/d\n\t");
    CHECK(*items[1].asString() == "\xc3\xa9");
    CHECK(*items[2].asString() == "\xf0\x9f\x98\x80");
    CHECK(*items[3].asString() == "C:\\Games\\Coney");
}

TEST_CASE("JSON numbers follow RFC 8259", "[json]") {
    CHECK(parse("-12")->asInteger() == -12);
    CHECK(parse("1e3")->asDouble() == 1000.0);
    CHECK_FALSE(parse("1e3")->asInteger().has_value());
    CHECK(parse("-0.25E-2")->asDouble() == -0.0025);
    // Beyond 64 bits it is still a number, without an exact integer.
    const auto big = parse("123456789012345678901234567890");
    REQUIRE(big.has_value());
    CHECK_FALSE(big->asInteger().has_value());
    for (const char* bad : {"01", "1.", ".5", "+1", "1e", "-", "0x10", "1e999"}) {
        INFO(bad);
        CHECK_FALSE(parse(bad).has_value());
    }
}

TEST_CASE("text JSON refuses is refused with where it went wrong", "[json]") {
    for (const char* bad : {"", "{", "[1,]", "{\"a\":1,}", "{\"a\" 1}", "{a: 1}", "'x'", "[1] 2", "// c\n1",
                            "\"tab\there\"", "\"\\x\"", "\"\\ud83d\"", "\"\\ude00\"", "{\"a\":1,\"a\":2}", "tru"}) {
        INFO(bad);
        const auto doc = parse(bad);
        REQUIRE_FALSE(doc.has_value());
        CHECK(doc.error().code == ErrorCode::Invalid);
        CHECK(doc.error().message.find("at byte") != std::string::npos);
    }
}

TEST_CASE("JSON nesting deeper than 64 levels is refused", "[json]") {
    CHECK(parse(std::string(64, '[') + std::string(64, ']')).has_value());
    CHECK_FALSE(parse(std::string(65, '[') + std::string(65, ']')).has_value());
}
