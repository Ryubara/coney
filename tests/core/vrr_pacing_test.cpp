// SPDX-License-Identifier: GPL-3.0-or-later
// The VRR frame-pacing mode (docs/engine/renderer.md#frame-rate-gaps): the cap chosen for a display's refresh rate,
// vsync off, `--fps-cap vrr` and the saved `Display/VRR`. Coney's own: the original presents at the PS2's field rate.
// Pure functions of a refresh rate given as a number, so no display and no clock are needed.
#include "core/vrr_pacing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/options.h"

using coney::choosePacing;
using coney::ErrorCode;
using coney::OptionSet;
using coney::parseOptions;
using coney::vrrFpsCap;

namespace {
// Parses a fixed list of arguments as the build with the debug tools does, or as the release build does.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args, OptionSet set = OptionSet::All) {
    return parseOptions(std::span<const std::string_view>(args), set);
}
} // namespace

// No test name starts with "-": ctest would hand a leading "--" to Catch2 as one of its own options.

TEST_CASE("vrr pacing caps just under the display's refresh rate", "[vrr][pacing]") {
    // Expected: ../engine/renderer.md#frame-rate-gaps, Coney's own choice: refresh - refresh^2 / 3600, rounded down
    CHECK(vrrFpsCap(60.0) == std::optional<std::uint32_t>(59));
    CHECK(vrrFpsCap(59.94) == std::optional<std::uint32_t>(58));
    CHECK(vrrFpsCap(75.0) == std::optional<std::uint32_t>(73));
    CHECK(vrrFpsCap(120.0) == std::optional<std::uint32_t>(116));
    CHECK(vrrFpsCap(144.0) == std::optional<std::uint32_t>(138));
    CHECK(vrrFpsCap(165.0) == std::optional<std::uint32_t>(157));
    CHECK(vrrFpsCap(240.0) == std::optional<std::uint32_t>(224));
    CHECK(vrrFpsCap(360.0) == std::optional<std::uint32_t>(324));
    // Always under the rate, so every frame stays inside the display's range.
    for (const double hz : {48.0, 60.0, 90.0, 100.0, 144.0, 200.0, 280.0, 500.0}) {
        const std::optional<std::uint32_t> cap = vrrFpsCap(hz);
        REQUIRE(cap.has_value());
        CHECK(static_cast<double>(cap.value_or(0)) < hz);
    }
}

TEST_CASE("vrr pacing has no cap for an unknown refresh rate and keeps within the largest cap", "[vrr][pacing]") {
    // Expected: ../guides/running-options.md#frame-rate, Coney's own choice
    // (SDL_DisplayMode::refresh_rate is 0 when unknown; kMaxFpsCap in core/options.h bounds every cap)
    CHECK_FALSE(vrrFpsCap(0.0).has_value());
    CHECK_FALSE(vrrFpsCap(-60.0).has_value());
    CHECK_FALSE(vrrFpsCap(std::numeric_limits<double>::quiet_NaN()).has_value());
    CHECK(vrrFpsCap(1000.0) == std::optional<std::uint32_t>(722));
    CHECK(vrrFpsCap(5000.0) == std::optional<std::uint32_t>(722));
    CHECK(vrrFpsCap(std::numeric_limits<double>::infinity()) == std::optional<std::uint32_t>(722));
}

TEST_CASE("vrr pacing turns vsync off and caps under the refresh rate", "[vrr][pacing]") {
    // Expected: ../engine/renderer.md#frame-rate-gaps, Coney's own choice (vsync off, a cap under the refresh rate)
    const coney::PacingChoice vrr = choosePacing(0, true, true, 144.0);
    CHECK(vrr.vrr);
    CHECK_FALSE(vrr.vsync);
    CHECK(vrr.fpsCap == 138);
    CHECK_FALSE(vrr.refreshUnknown);
    // Without VRR the cap and vsync are as given.
    const coney::PacingChoice plain = choosePacing(60, false, false, 144.0);
    CHECK_FALSE(plain.vrr);
    CHECK_FALSE(plain.vsync);
    CHECK(plain.fpsCap == 60);
    const coney::PacingChoice defaults = choosePacing(0, true, false, 0.0);
    CHECK(defaults.vsync);
    CHECK(defaults.fpsCap == 0);
}

TEST_CASE("vrr pacing with no known refresh rate keeps the cap and vsync as given", "[vrr][pacing]") {
    // Expected: ../guides/running-options.md#frame-rate, Coney's own choice
    // (vsync off with no cap would tear on a display whose range is unknown)
    const coney::PacingChoice unknown = choosePacing(0, true, true, 0.0);
    CHECK_FALSE(unknown.vrr);
    CHECK(unknown.vsync);
    CHECK(unknown.fpsCap == 0);
    CHECK(unknown.refreshUnknown);
}

TEST_CASE("the fps cap option takes vrr in both builds", "[vrr][options]") {
    // Expected: ../guides/running-options.md#frame-rate, Coney's own option
    const auto all = parse(std::array<std::string_view, 2>{"--fps-cap", "vrr"});
    REQUIRE(all.has_value());
    CHECK(all->vrrPacing);
    CHECK_FALSE(all->fpsCap.has_value());
    const auto player = parse(std::array<std::string_view, 2>{"--fps-cap", "vrr"}, OptionSet::Player);
    REQUIRE(player.has_value());
    CHECK(player->vrrPacing);
    // A number is still a number.
    const auto number = parse(std::array<std::string_view, 2>{"--fps-cap", "144"});
    REQUIRE(number.has_value());
    CHECK_FALSE(number->vrrPacing);
    CHECK(number->fpsCap == std::optional<int>(144));
    // Anything else is refused, and so is a second --fps-cap.
    const auto bad = parse(std::array<std::string_view, 2>{"--fps-cap", "VRR"});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    CHECK(!parse(std::array<std::string_view, 4>{"--fps-cap", "vrr", "--fps-cap", "60"}).has_value());
}

TEST_CASE("the vrr fps cap is refused in test mode and beside vsync", "[vrr][options]") {
    // Expected: ../guides/running-options.md#frame-rate, Coney's own option (test mode keeps no real clock)
    // Taken alone in a windowed run, so each refusal below is the combination's.
    REQUIRE(parse(std::array<std::string_view, 2>{"--fps-cap", "vrr"}).has_value());
    CHECK(!parse(std::array<std::string_view, 3>{"--fps-cap", "vrr", "--headless"}).has_value());
    CHECK(!parse(std::array<std::string_view, 4>{"--fps-cap", "vrr", "--frames", "10"}).has_value());
    // VRR sets vsync itself, so a --vsync beside it, either way, is refused rather than ignored.
    CHECK(!parse(std::array<std::string_view, 4>{"--fps-cap", "vrr", "--vsync", "on"}).has_value());
    CHECK(!parse(std::array<std::string_view, 4>{"--vsync", "off", "--fps-cap", "vrr"}).has_value());
}

TEST_CASE("vrr pacing comes from the option or the saved setting outside test mode", "[vrr][options]") {
    // Expected: ../guides/running-options.md#frame-rate, Coney's own option (the saved Display/VRR is a player's)
    const auto none = parse(std::array<std::string_view, 0>{});
    REQUIRE(none.has_value());
    CHECK_FALSE(coney::wantsVrr(*none, false));
    CHECK(coney::wantsVrr(*none, true));
    const auto option = parse(std::array<std::string_view, 2>{"--fps-cap", "vrr"});
    REQUIRE(option.has_value());
    CHECK(coney::wantsVrr(*option, false));
    // A cap asked for by number overrides the saved mode.
    const auto number = parse(std::array<std::string_view, 2>{"--fps-cap", "60"});
    REQUIRE(number.has_value());
    CHECK_FALSE(coney::wantsVrr(*number, true));
    // Test mode ignores what a player saved.
    const auto headless = parse(std::array<std::string_view, 1>{"--headless"});
    REQUIRE(headless.has_value());
    CHECK_FALSE(coney::wantsVrr(*headless, true));
}

TEST_CASE("vrr pacing says in the run log what it chose or why it could not", "[vrr][pacing]") {
    // Expected: ../guides/running-options.md#frame-rate, Coney's own run log line (none without VRR)
    CHECK(coney::vrrLogLine(choosePacing(0, true, true, 144.0), 144.0) ==
          "VRR pacing: vsync off, at most 138 frames a second, under the display's 144 Hz");
    CHECK(coney::vrrLogLine(choosePacing(0, true, true, 0.0), 0.0) ==
          "VRR pacing: the display's refresh rate is unknown, so the frame rate is paced as without it");
    CHECK(coney::vrrLogLine(choosePacing(60, false, false, 144.0), 144.0).empty());
}
