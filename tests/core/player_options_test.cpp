// SPDX-License-Identifier: GPL-3.0-or-later
// The release build's command line: the options a player's build takes, and the refusal of the developers' ones
// (docs/guides/building.md#debug-tools, docs/guides/running.md).
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/options.h"

using coney::OptionSet;
using coney::parseOptions;

namespace {

// Parses `args` as the release build does.
template <std::size_t N> auto parsePlayer(const std::array<std::string_view, N>& args) {
    return parseOptions(std::span<const std::string_view>(args), OptionSet::Player);
}

// Every option the full usage text names (`--name` at the start of an indented line), as typed.
std::vector<std::string> allOptions() {
    std::vector<std::string> names;
    const std::string_view usage = coney::usageText();
    std::size_t at = 0;
    while (at < usage.size()) {
        const std::size_t end = usage.find('\n', at);
        const std::string_view line = usage.substr(at, end == std::string_view::npos ? usage.size() - at : end - at);
        if (line.starts_with("  --")) {
            const std::string_view rest = line.substr(2);
            names.emplace_back(rest.substr(0, rest.find(' ')));
        }
        at = end == std::string_view::npos ? usage.size() : end + 1;
    }
    return names;
}

} // namespace

TEST_CASE("the release build takes a player's options", "[options]") {
    const std::array<std::string_view, 17> args{
        "--disc",  "game.iso",      "--language",   "fr",         "--fps-cap",     "60",
        "--vsync", "off",           "--no-audio",   "--show-fps", "--skip-movies", "--profiles",
        "saves",   "--no-activate", "--line-blend", "on",         "--help"};
    auto options = parsePlayer(args);
    REQUIRE(options.has_value());
    CHECK(options->discPath == "game.iso");
    CHECK(options->noAudio);
    CHECK(options->showFps);
    CHECK(options->skipMovies);
    CHECK(options->showHelp);
}

TEST_CASE("the release build keeps test mode for its smoke tests", "[options]") {
    const std::array<std::string_view, 5> args{"--headless", "--frames", "10", "--input-script", "pad.txt"};
    auto options = parsePlayer(args);
    REQUIRE(options.has_value());
    CHECK(options->headless);
    CHECK(options->frameLimit == 10);
    CHECK(options->inputScript == "pad.txt");
}

TEST_CASE("the release build refuses a developer's option and names the debug tools", "[options]") {
    const std::array<std::string_view, 4> args{"--disc", "game.iso", "--play-level", "level99"};
    auto options = parsePlayer(args);
    REQUIRE_FALSE(options.has_value());
    CHECK(options.error().code == coney::ErrorCode::InvalidArgument);
    CHECK(options.error().message.find("--play-level") != std::string::npos);
    CHECK(options.error().message.find("debug tools") != std::string::npos);
    // The full set takes it.
    CHECK(parseOptions(std::span<const std::string_view>(args)).has_value());
}

TEST_CASE("every option is either a player's or refused by the release build", "[options]") {
    const std::vector<std::string> names = allOptions();
    REQUIRE(names.size() > 40);
    std::size_t player = 0;
    for (const std::string& name : names) {
        if (coney::isPlayerOption(name)) {
            ++player;
            continue;
        }
        const std::array<std::string_view, 1> args{name};
        auto options = parsePlayer(args);
        INFO(name);
        REQUIRE_FALSE(options.has_value());
        CHECK(options.error().message.find("debug tools") != std::string::npos);
    }
    CHECK(player == coney::playerOptions().size());
}

TEST_CASE("the release build's usage text lists only a player's options", "[options]") {
    const std::string usage = coney::usageText(OptionSet::Player);
    CHECK(usage.find("--disc") != std::string::npos);
    CHECK(usage.find("--skip-movies") != std::string::npos);
    CHECK(usage.find("--frames") != std::string::npos);
    CHECK(usage.find("--host") != std::string::npos);
    CHECK(usage.find("--join") != std::string::npos);
    CHECK(usage.find("--net-name") != std::string::npos);
    CHECK(usage.find("--play-level") == std::string::npos);
    CHECK(usage.find("--sandbox") == std::string::npos);
    CHECK(usage.find("--tunables") == std::string::npos);
    CHECK(usage.find("--view-character") == std::string::npos);
    CHECK(usage.ends_with('\n'));
    // A continuation line stays with its option: --input-script's description follows it.
    CHECK(usage.find("--input-script") < usage.find("--headless"));
}

TEST_CASE("the release build takes network play's options", "[options][net]") {
    // Network play ships to players (docs/guides/multiplayer.md), so a player's build hosts and joins from the
    // command line as the debug tools' build does.
    const std::array<std::string_view, 4> host{"--host", "127.0.0.1:27615", "--net-name", "Tester"};
    auto hosting = parsePlayer(host);
    REQUIRE(hosting.has_value());
    REQUIRE(hosting->netHost.has_value());
    CHECK(hosting->netHost->port == 27615);
    CHECK(hosting->netName == "Tester");
    const std::array<std::string_view, 2> join{"--join", "127.0.0.1"};
    auto joining = parsePlayer(join);
    REQUIRE(joining.has_value());
    REQUIRE(joining->netJoin.has_value());
    CHECK(joining->netJoin->address == "127.0.0.1");
}
