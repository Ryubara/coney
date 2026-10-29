// SPDX-License-Identifier: GPL-3.0-or-later
// The --world-edit and --record-debug-draw options' parsing (docs/guides/running-viewers.md#world-edits,
// docs/guides/running-options.md#record); the edit file they name is tested in tests/debug/world_split_test.cpp.
#include "core/options.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {
// Parses a fixed list of arguments, sparing each test the span conversion.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return coney::parseOptions(std::span<const std::string_view>(args));
}
} // namespace

TEST_CASE("the world edit option takes a file once, with the world viewer or a level", "[options]") {
    // Expected: unverified, #1123 (Coney's own debug option, docs/guides/running-viewers.md#world-edits)
    auto viewer = parse(std::array<std::string_view, 6>{"--disc", "d", "--view-world", "level99", "--world-edit", "e"});
    REQUIRE(viewer.has_value());
    CHECK(viewer->worldEditFile == "e");
    auto level = parse(std::array<std::string_view, 6>{"--disc", "d", "--play-level", "level99", "--world-edit", "e"});
    REQUIRE(level.has_value());
    CHECK(level->worldEditFile == "e");
    CHECK_FALSE(parse(std::array<std::string_view, 0>{})->worldEditFile.has_value());
    // Nothing to edit: no world, a sandbox, a value missing, or the option twice.
    CHECK_FALSE(parse(std::array<std::string_view, 2>{"--world-edit", "e"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 6>{"--disc", "d", "--play-level", "sandbox:lane", "--world-edit", "e"})
            .has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 5>{"--disc", "d", "--view-world", "level99", "--world-edit"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 8>{"--disc", "d", "--view-world", "level99", "--world-edit", "e",
                                                      "--world-edit", "f"})
                    .has_value());
    CHECK(coney::usageText().find("--world-edit FILE") != std::string_view::npos);
}

TEST_CASE("the recording keeps the scene's debug lines only when asked, and only with --record", "[options]") {
    // Expected: unverified, #1123 (Coney's own debug option, docs/guides/running-options.md#record)
    CHECK_FALSE(parse(std::array<std::string_view, 0>{})->recordDebugDraw);
    auto given = parse(std::array<std::string_view, 3>{"--record", "s/t/clips/a.mp4", "--record-debug-draw"});
    REQUIRE(given.has_value());
    CHECK(given->recordDebugDraw);
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--record-debug-draw"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--record", "s/t/clips/a.mp4", "--record-debug-draw",
                                                      "--record-debug-draw"})
                    .has_value());
    CHECK(coney::usageText().find("--record-debug-draw") != std::string_view::npos);
}
