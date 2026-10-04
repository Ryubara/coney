// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/options.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"

using coney::ErrorCode;
using coney::parseOptions;

namespace {
// Parses a fixed list of arguments, sparing each test the span conversion.
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

TEST_CASE("giving --disc and --load collects the disc and the entries in order", "[options]") {
    auto result = parse(std::array<std::string_view, 6>{"--disc", "H:\\", "--load", "level1.lev", "--load", "0x1"});
    REQUIRE(result.has_value());
    CHECK(result->discPath == "H:\\");
    CHECK(result->loads == std::vector<std::string>{"level1.lev", "0x1"});
}

TEST_CASE("giving --load without --disc is an error naming --disc", "[options]") {
    auto result = parse(std::array<std::string_view, 2>{"--load", "level1.lev"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("--disc") != std::string::npos);
}

TEST_CASE("giving --disc or --load without a value is an error", "[options]") {
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--disc"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--load"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--disc", "x", "--disc", "y"}).has_value());
}

TEST_CASE("the usage text documents the disc options", "[options]") {
    CHECK(coney::usageText().find("--disc") != std::string_view::npos);
    CHECK(coney::usageText().find("--load") != std::string_view::npos);
}

TEST_CASE("giving --headless, --view-txd and --screenshot sets them", "[options]") {
    auto viewer = parse(std::array<std::string_view, 8>{"--disc", "H:\\", "--view-txd", "0x1234", "--frames", "3",
                                                        "--screenshot", "out.png"});
    REQUIRE(viewer.has_value());
    CHECK(viewer->viewTxd == "0x1234");
    CHECK(viewer->screenshotPath == "out.png");
    CHECK_FALSE(viewer->headless);
    auto headless = parse(std::array<std::string_view, 3>{"--headless", "--frames", "1"});
    REQUIRE(headless.has_value());
    CHECK(headless->headless);
}

TEST_CASE("the viewer and screenshot options refuse what cannot work", "[options]") {
    // --view-txd needs --disc and a value, once, and does not go with --load.
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--view-txd", "a"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--view-txd"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "x", "--view-txd", "a", "--view-txd", "b"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--disc", "x", "--view-txd", "a", "--load", "b"}).has_value());
    // --screenshot needs --frames and a window.
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--screenshot", "a.png"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 5>{"--screenshot", "a.png", "--frames", "1", "--headless"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--screenshot"}).has_value());
}

TEST_CASE("--help wins over options that do not go together", "[options]") {
    auto result = parse(std::array<std::string_view, 3>{"--view-txd", "a", "--help"});
    REQUIRE(result.has_value());
    CHECK(result->showHelp);
}

TEST_CASE("the usage text documents the viewer options", "[options]") {
    CHECK(coney::usageText().find("--view-txd") != std::string_view::npos);
    CHECK(coney::usageText().find("--screenshot") != std::string_view::npos);
    CHECK(coney::usageText().find("--headless") != std::string_view::npos);
}

TEST_CASE("the sheet viewer option takes a sheet and needs a disc and no other viewer", "[options]") {
    auto viewer = parse(std::array<std::string_view, 4>{"--disc", "H:\\", "--view-sheet", "menu_system"});
    REQUIRE(viewer.has_value());
    CHECK(viewer->viewSheet == "menu_system");
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--view-sheet", "a"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--disc", "x", "--view-sheet"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "x", "--view-sheet", "a", "--view-txd", "b"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--disc", "x", "--view-sheet", "a", "--load", "b"}).has_value());
    CHECK(coney::usageText().find("--view-sheet") != std::string_view::npos);
}
