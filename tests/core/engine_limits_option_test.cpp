// SPDX-License-Identifier: GPL-3.0-or-later
// `--engine-limits coney|original` and the rule that pins test mode at the original's limits
// (docs/guides/running-options.md#engine-limits).
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/engine_limits.h"
#include "core/error.h"
#include "core/options.h"

using coney::EngineLimits;
using coney::ErrorCode;
using coney::parseOptions;

namespace {
// Parses a fixed list of arguments as the build with the debug tools does.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return parseOptions(std::span<const std::string_view>(args));
}

// The limits a run of `args` plays under; asserts that they parse.
template <std::size_t N> EngineLimits limitsFor(const std::array<std::string_view, N>& args) {
    auto options = parse(args);
    REQUIRE(options.has_value());
    return options ? coney::engineLimitsFor(*options) : EngineLimits::Coney;
}
} // namespace

// No test name starts with "-": ctest would hand a leading "--" to Catch2 as one of its own options.

TEST_CASE("the engine limits names are coney and original, and nothing else", "[options][engine-limits]") {
    // Expected: unverified, #1591 (Coney's own option: docs/guides/running-options.md#engine-limits)
    CHECK(coney::parseEngineLimitsName("coney") == std::optional<EngineLimits>(EngineLimits::Coney));
    CHECK(coney::parseEngineLimitsName("original") == std::optional<EngineLimits>(EngineLimits::Original));
    CHECK_FALSE(coney::parseEngineLimitsName("ps2").has_value());
    CHECK_FALSE(coney::parseEngineLimitsName("Original").has_value());
    CHECK_FALSE(coney::parseEngineLimitsName("").has_value());
    CHECK(coney::engineLimitsName(EngineLimits::Coney) == "coney");
    CHECK(coney::engineLimitsName(EngineLimits::Original) == "original");
}

TEST_CASE("the engine limits option takes coney or original and refuses anything else", "[options][engine-limits]") {
    // Expected: unverified, #1591 (Coney's own option: docs/guides/running-options.md#engine-limits)
    auto plain = parseOptions({});
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->engineLimits.has_value());
    auto original = parse(std::array<std::string_view, 2>{"--engine-limits", "original"});
    REQUIRE(original.has_value());
    CHECK(original->engineLimits == std::optional<EngineLimits>(EngineLimits::Original));
    auto coneys = parse(std::array<std::string_view, 2>{"--engine-limits", "coney"});
    REQUIRE(coneys.has_value());
    CHECK(coneys->engineLimits == std::optional<EngineLimits>(EngineLimits::Coney));
    auto bad = parse(std::array<std::string_view, 2>{"--engine-limits", "ps2"});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    CHECK(bad.error().message.find("coney or original") != std::string::npos);
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--engine-limits"}).has_value());
    CHECK_FALSE(
        parse(std::array<std::string_view, 4>{"--engine-limits", "coney", "--engine-limits", "original"}).has_value());
    CHECK(coney::usageText().find("--engine-limits coney|original") != std::string_view::npos);
}

TEST_CASE("test mode plays at the original's limits unless the option or a bench asks for Coney's",
          "[options][engine-limits][test-mode]") {
    // Expected: unverified, #1591 (Coney's own rule: docs/guides/running-options.md#engine-limits): a run that can be
    // compared with the PS2 one plays under the PS2's ceilings; a run a person plays, and a bench, which measures
    // Coney's own crowd rules, lift them.
    // A run a person plays: Coney's own rule.
    CHECK(limitsFor(std::array<std::string_view, 0>{}) == EngineLimits::Coney);
    CHECK(limitsFor(std::array<std::string_view, 4>{"--disc", "x", "--play-level", "level99"}) == EngineLimits::Coney);
    // Test mode: the engine limits preset.
    CHECK(limitsFor(std::array<std::string_view, 1>{"--headless"}) == EngineLimits::Original);
    CHECK(limitsFor(std::array<std::string_view, 2>{"--frames", "3"}) == EngineLimits::Original);
    CHECK(limitsFor(std::array<std::string_view, 2>{"--input-script", "pad.txt"}) == EngineLimits::Original);
    CHECK(limitsFor(std::array<std::string_view, 1>{"--pad-pipe"}) == EngineLimits::Original);
    CHECK(limitsFor(std::array<std::string_view, 2>{"--digest", "d.txt"}) == EngineLimits::Original);
    // A bench measures Coney's own crowd rules, so it lifts them unless told otherwise.
    const std::array<std::string_view, 8> bench{"--disc",   "x",   "--play-level", "level95",
                                                "--frames", "300", "--bench",      "out.json"};
    CHECK(limitsFor(bench) == EngineLimits::Coney);
    // Named, the option wins everywhere.
    CHECK(limitsFor(std::array<std::string_view, 4>{"--frames", "3", "--engine-limits", "coney"}) ==
          EngineLimits::Coney);
    CHECK(limitsFor(std::array<std::string_view, 2>{"--engine-limits", "original"}) == EngineLimits::Original);
    const std::array<std::string_view, 10> pinnedBench{"--disc",          "x",       "--play-level", "level95",
                                                       "--frames",        "300",     "--bench",      "out.json",
                                                       "--engine-limits", "original"};
    CHECK(limitsFor(pinnedBench) == EngineLimits::Original);
}
