// SPDX-License-Identifier: GPL-3.0-or-later
// `--preset ps2|xbox|enhanced|high` on the command line (docs/guides/enhancements.md#where-the-settings-live) and the
// pin that keeps test mode on the PS2 picture (docs/engine/renderer.md#ps2-preset).
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/display_preset.h"
#include "core/error.h"
#include "core/options.h"

using coney::DisplayPreset;
using coney::ErrorCode;
using coney::OptionSet;
using coney::parseOptions;
using coney::pinnedDisplayPreset;

namespace {
// Parses a fixed list of arguments as the build with the debug tools does.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return parseOptions(std::span<const std::string_view>(args));
}

// The preset a run of `args` pins drawing to; asserts that they parse.
template <std::size_t N> std::optional<DisplayPreset> pinnedFor(const std::array<std::string_view, N>& args) {
    auto options = parse(args);
    REQUIRE(options.has_value());
    return pinnedDisplayPreset(*options);
}
} // namespace

// No test name starts with "-": ctest would hand a leading "--" to Catch2 as one of its own options.

TEST_CASE("the preset option takes ps2, xbox, enhanced or high and refuses anything else", "[options][display]") {
    // Expected: ../guides/enhancements.md#where-the-settings-live, Coney's own option
    auto plain = parseOptions({});
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->displayPreset.has_value());
    auto xbox = parse(std::array<std::string_view, 2>{"--preset", "xbox"});
    REQUIRE(xbox.has_value());
    CHECK(xbox->displayPreset == std::optional<DisplayPreset>(DisplayPreset::Xbox));
    auto high = parse(std::array<std::string_view, 2>{"--preset", "high"});
    REQUIRE(high.has_value());
    CHECK(high->displayPreset == std::optional<DisplayPreset>(DisplayPreset::High));
    auto bad = parse(std::array<std::string_view, 2>{"--preset", "custom"});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    CHECK(bad.error().message.find("ps2, xbox, enhanced or high") != std::string::npos);
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--preset"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--preset", "ps2", "--preset", "xbox"}).has_value());
    CHECK(coney::usageText().find("--preset ps2|xbox|enhanced|high") != std::string_view::npos);
    // A player's release build takes it too.
    const std::array<std::string_view, 2> args{"--preset", "enhanced"};
    auto player = parseOptions(std::span<const std::string_view>(args), OptionSet::Player);
    REQUIRE(player.has_value());
    CHECK(player->displayPreset == std::optional<DisplayPreset>(DisplayPreset::Enhanced));
    CHECK(coney::usageText(OptionSet::Player).find("--preset ps2|xbox|enhanced|high") != std::string::npos);
}

TEST_CASE("test mode pins the PS2 preset unless the preset option names another", "[options][display][test-mode]") {
    // Expected: ../engine/renderer.md#ps2-preset, Coney's own design: --headless, --frames, --screenshot, --record and
    // the disc tests draw PS2 whatever a player saved.
    // A run a person plays draws what they saved: nothing pinned.
    CHECK_FALSE(pinnedFor(std::array<std::string_view, 0>{}).has_value());
    const std::optional<DisplayPreset> ps2 = DisplayPreset::Ps2;
    CHECK(pinnedFor(std::array<std::string_view, 1>{"--headless"}) == ps2);
    CHECK(pinnedFor(std::array<std::string_view, 2>{"--frames", "3"}) == ps2);
    CHECK(pinnedFor(std::array<std::string_view, 4>{"--frames", "1", "--screenshot", "a.png"}) == ps2);
    CHECK(pinnedFor(std::array<std::string_view, 2>{"--record", "scratch/task/clips/a.mp4"}) == ps2);
    CHECK(pinnedFor(std::array<std::string_view, 2>{"--input-script", "pad.txt"}) == ps2);
    CHECK(pinnedFor(std::array<std::string_view, 4>{"--disc", "x", "--render-references", "out"}) == ps2);
    // Asked for by name, another preset is drawn even in test mode: a capture or a benchmark of that preset.
    CHECK(pinnedFor(std::array<std::string_view, 4>{"--frames", "3", "--preset", "xbox"}) ==
          std::optional<DisplayPreset>(DisplayPreset::Xbox));
    // Outside test mode the option is a choice at the start, which the player may change: nothing pinned.
    CHECK_FALSE(pinnedFor(std::array<std::string_view, 2>{"--preset", "high"}).has_value());
}
