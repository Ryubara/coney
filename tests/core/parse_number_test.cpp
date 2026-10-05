// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/parse_number.h"

#include <optional>

#include <catch2/catch_test_macros.hpp>

using coney::parseDecimal;

TEST_CASE("parseDecimal reads whole decimal numbers", "[parse_number]") {
    CHECK(parseDecimal("3") == 3.0);
    CHECK(parseDecimal("-1.25") == -1.25);
    CHECK(parseDecimal("0.5") == 0.5);
    CHECK(parseDecimal("2e-3") == 2e-3);
}

TEST_CASE("parseDecimal refuses what from_chars refuses on every platform", "[parse_number]") {
    for (const char* text : {"", " 1", "+1", "1 ", "1x", "0x10", "inf", "-inf", "nan", "1e999", "abc", "."}) {
        INFO(text);
        CHECK(parseDecimal(text) == std::nullopt);
    }
}
