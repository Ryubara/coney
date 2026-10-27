// SPDX-License-Identifier: GPL-3.0-or-later
// The `--bench` options that set up the crowd and how a bench is measured (docs/guides/performance.md#bench).
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/options.h"

namespace {

// Parses `args` as the command line after the program's name.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return coney::parseOptions(std::span<const std::string_view>(args));
}

} // namespace

// Expected: docs/guides/performance.md#bench
TEST_CASE("the bench options spawn allies, make the player invincible and keep every frame", "[options]") {
    auto bench = parse(std::array<std::string_view, 15>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                        "--bench", "out.json", "--bench-allies", "50", "--bench-god",
                                                        "--bench-frames", "frames.csv", "--bench-fighters", "50"});
    REQUIRE(bench.has_value());
    CHECK(bench->benchAllies == std::optional<int>(50));
    CHECK(bench->benchGod);
    CHECK(bench->benchFramesFile == std::optional<std::string>("frames.csv"));
    // None of them by default.
    auto plain = parse(std::array<std::string_view, 8>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                       "--bench", "out.json"});
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->benchAllies.has_value());
    CHECK_FALSE(plain->benchGod);
    CHECK_FALSE(plain->benchFramesFile.has_value());
    // The allies from 0 to 1000, and each only with --bench.
    CHECK_FALSE(parse(std::array<std::string_view, 10>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                       "--bench", "out.json", "--bench-allies", "1001"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 8>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                      "--bench-allies", "10"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 7>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                      "--bench-god"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 8>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                      "--bench-frames", "f.csv"})
                    .has_value());
    // Real-time pacing, only with --bench.
    auto paced = parse(std::array<std::string_view, 9>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                       "--bench", "out.json", "--bench-real-time"});
    REQUIRE(paced.has_value());
    CHECK(paced->benchRealTime);
    CHECK_FALSE(plain->benchRealTime);
    CHECK_FALSE(parse(std::array<std::string_view, 7>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                      "--bench-real-time"})
                    .has_value());
    CHECK(coney::usageText().find("--bench-real-time") != std::string_view::npos);
    // The spawn after a number of steps of play, only with --bench.
    auto later = parse(std::array<std::string_view, 10>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                        "--bench", "out.json", "--bench-spawn-step", "120"});
    REQUIRE(later.has_value());
    CHECK(later->benchSpawnStep == std::optional<int>(120));
    CHECK_FALSE(plain->benchSpawnStep.has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 8>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                      "--bench-spawn-step", "10"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 10>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                       "--bench", "out.json", "--bench-spawn-step", "-1"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 10>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                       "--bench", "out.json", "--bench-spawn-step", "250"})
                    .has_value());
    CHECK(coney::usageText().find("--bench-spawn-step") != std::string_view::npos);
    // A number of frames to measure, then the run ends; only with --bench, and at least 1.
    auto measured = parse(std::array<std::string_view, 10>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                           "--bench", "out.json", "--bench-measure", "100"});
    REQUIRE(measured.has_value());
    CHECK(measured->benchMeasure == std::optional<int>(100));
    CHECK_FALSE(plain->benchMeasure.has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 8>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                      "--bench-measure", "10"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 10>{"--disc", "x", "--play-level", "level95", "--frames", "300",
                                                       "--bench", "out.json", "--bench-measure", "0"})
                    .has_value());
    CHECK(coney::usageText().find("--bench-measure") != std::string_view::npos);
    CHECK(coney::usageText().find("--bench-allies") != std::string_view::npos);
    CHECK(coney::usageText().find("--bench-frames") != std::string_view::npos);
}
