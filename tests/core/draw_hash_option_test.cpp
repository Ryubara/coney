// SPDX-License-Identifier: GPL-3.0-or-later
// `--draw-hash`: the option that prints a run's last-frame draw stream (docs/engine/renderer.md#comparing). The other
// options' tests are in options_test.cpp.
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/options.h"

using coney::parseOptions;

namespace {
// Parses a fixed list of arguments, sparing each test the span conversion.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return parseOptions(std::span<const std::string_view>(args));
}
} // namespace

TEST_CASE("the draw hash option needs frames and a run that draws frames", "[options]") {
    // Expected: --draw-hash prints the last frame's stream (docs/engine/renderer.md#comparing), so it needs --frames,
    // works headless and with a window, and not with the console job --load or with --render-references.
    auto headless = parse(std::array<std::string_view, 4>{"--frames", "3", "--draw-hash", "--headless"});
    REQUIRE(headless.has_value());
    CHECK(headless->drawHash);
    auto windowed = parse(std::array<std::string_view, 3>{"--draw-hash", "--frames", "1"});
    REQUIRE(windowed.has_value());
    CHECK(windowed->drawHash);
    CHECK_FALSE(parse(std::array<std::string_view, 0>{}).value_or(coney::Options{}).drawHash);
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--draw-hash"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--draw-hash", "--draw-hash", "--frames", "1"}).has_value());
    CHECK(coney::usageText().find("--draw-hash") != std::string_view::npos);
}
