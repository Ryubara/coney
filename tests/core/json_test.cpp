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

TEST_CASE("JSON that RFC 8259 refuses is refused, naming the byte", "[json]") {
    for (const char* bad : {"", "{", "[1,]", "{\"a\":1,}", "{\"a\" 1}", "{a: 1}", "'x'", "[1] 2", "// c\n1",
                            "\"tab\there\"", "\"\\x\"", "\"\\ud83d\"", "\"\\ude00\"", "{\"a\":1,\"a\":2}", "tru"}) {
        INFO(bad);
        const auto doc = parse(bad);
        REQUIRE_FALSE(doc.has_value());
        CHECK(doc.error().code == ErrorCode::Invalid);
        CHECK(doc.error().message.find("at byte") != std::string::npos);
    }
}

TEST_CASE("JSON strings must be UTF-8", "[json]") {
    // Two, three and four bytes, at the edges of what each length may hold.
    const auto good =
        parse("[\"\xc2\x80\", \"\xe0\xa0\x80\", \"\xed\x9f\xbf\", \"\xee\x80\x80\", \"\xf0\x90\x80\x80\", "
              "\"\xf4\x8f\xbf\xbf\", \"caf\xc3\xa9\"]");
    REQUIRE(good.has_value());
    CHECK(*(*good->asArray())[1].asString() == "\xe0\xa0\x80");
    CHECK(*(*good->asArray())[4].asString() == "\xf0\x90\x80\x80");
    CHECK(*(*good->asArray())[6].asString() == "caf\xc3\xa9");
    // Each bad sequence starts after `"ab`, at byte 3, which the error names.
    struct Bad {
        const char* text;
        const char* why;
    };
    for (const Bad& bad :
         {Bad{"\"ab\x80\"", "a stray continuation byte"}, Bad{"\"ab\xc0\xaf\"", "an overlong '/'"},
          Bad{"\"ab\xe0\x80\xaf\"", "an overlong three-byte form"}, Bad{"\"ab\xed\xa0\x80\"", "a surrogate, U+D800"},
          Bad{"\"ab\xf4\x90\x80\x80\"", "past U+10FFFF"}, Bad{"\"ab\xf5\x80\x80\x80\"", "lead F5"},
          Bad{"\"ab\xf6\x80\x80\x80\"", "lead F6"}, Bad{"\"ab\xf7\x80\x80\x80\"", "lead F7"},
          Bad{"\"ab\xff\"", "a byte that never starts a sequence"}, Bad{"\"ab\xe2\x82\"", "cut short by the quote"},
          Bad{"\"ab\xf0\x9f\x98", "cut short by the end of the text"}}) {
        INFO(bad.why);
        const auto doc = parse(bad.text);
        REQUIRE_FALSE(doc.has_value());
        CHECK(doc.error().message.find("not UTF-8 at byte 3") != std::string::npos);
    }
}

TEST_CASE("JSON nesting deeper than 64 levels is refused", "[json]") {
    CHECK(parse(std::string(64, '[') + std::string(64, ']')).has_value());
    CHECK_FALSE(parse(std::string(65, '[') + std::string(65, ']')).has_value());
}
