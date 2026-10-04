// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/options.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"

using coney::ErrorCode;
using coney::parseOptions;

namespace {
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return parseOptions(std::span<const std::string_view>(args));
}
} // namespace

// No test name starts with "-": ctest runs each case as `coney_tests "<name>"`, and Catch2 would read a leading "--"
// as one of its own options (a case named "--help ..." would print Catch2's help and pass without running).

TEST_CASE("no arguments runs until the window closes", "[options]") {
    auto result = parseOptions({});
    REQUIRE(result.has_value());
    CHECK_FALSE(result->frameLimit.has_value());
    CHECK_FALSE(result->showHelp);
}

TEST_CASE("giving --frames N sets the frame limit", "[options]") {
    auto result = parse(std::array<std::string_view, 2>{"--frames", "3"});
    REQUIRE(result.has_value());
    CHECK(result->frameLimit == 3);
}

TEST_CASE("giving --frames the largest allowed value is accepted", "[options]") {
    auto result = parse(std::array<std::string_view, 2>{"--frames", "1000000"});
    REQUIRE(result.has_value());
    CHECK(result->frameLimit == coney::kMaxFrameLimit);
}

TEST_CASE("giving --help is recognised", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"--help"});
    REQUIRE(result.has_value());
    CHECK(result->showHelp);
}

TEST_CASE("giving --frames without a value is an error naming --frames", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"--frames"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    CHECK(result.error().message.find("--frames") != std::string::npos);
}

TEST_CASE("giving --frames a value that is not a positive whole number in range is an error", "[options]") {
    for (std::string_view bad : {"abc", "0", "-3", "3x", "", "1000001", "99999999999", " 3"}) {
        CAPTURE(bad);
        auto result = parse(std::array<std::string_view, 2>{"--frames", bad});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
        CHECK(result.error().message.find("--frames") != std::string::npos);
    }
}

TEST_CASE("an unknown option is an error naming it", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"--fullscreen"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("--fullscreen") != std::string::npos);
}

TEST_CASE("a stray positional argument is an error naming it", "[options]") {
    auto result = parse(std::array<std::string_view, 1>{"game.iso"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("game.iso") != std::string::npos);
}

TEST_CASE("giving --frames twice is an error", "[options]") {
    auto result = parse(std::array<std::string_view, 4>{"--frames", "3", "--frames", "4"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("--frames") != std::string::npos);
}

TEST_CASE("the usage text documents every option", "[options]") {
    CHECK(coney::usageText().find("--frames") != std::string_view::npos);
    CHECK(coney::usageText().find("--help") != std::string_view::npos);
}
