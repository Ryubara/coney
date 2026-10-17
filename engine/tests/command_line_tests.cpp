#include "core/command_line.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace coney::core;

namespace {
coney::Result<CommandLine, std::string> parse(std::vector<std::string_view> args) { return parse_command_line(args); }
} // namespace

TEST_CASE("no options give the default settings") {
    const auto parsed = parse({});
    REQUIRE(parsed);
    CHECK_FALSE(parsed->frames.has_value());
    CHECK(parsed->settings.loop.ticks_per_second == kGameTicksPerSecond);
    CHECK(parsed->settings.log.level == LogLevel::Info);
    CHECK(parsed->settings.log.file.empty());
}

TEST_CASE("options override the settings") {
    const auto parsed = parse({"--frames", "12", "--width", "320", "--height", "240", "--title", "t",
                               "--max-catch-up", "2", "--log-level", "debug", "--log-file", "out.log"});
    REQUIRE(parsed);
    CHECK(parsed->frames == 12u);
    CHECK(parsed->settings.window.width == 320);
    CHECK(parsed->settings.window.height == 240);
    CHECK(parsed->settings.window.title == "t");
    CHECK(parsed->settings.loop.max_catch_up_ticks == 2);
    CHECK(parsed->settings.log.level == LogLevel::Debug);
    CHECK(parsed->settings.log.file == "out.log");
}

TEST_CASE("bad options are reported") {
    CHECK_FALSE(parse({"--frames"}));
    CHECK_FALSE(parse({"--frames", "0"}));
    CHECK_FALSE(parse({"--frames", "3x"}));
    CHECK_FALSE(parse({"--width", "-5"}));
    CHECK_FALSE(parse({"--log-level", "loud"}));
    CHECK_FALSE(parse({"--colour", "red"}));
    CHECK(parse({"--colour", "red"}).error() == "unknown option: --colour");
}

TEST_CASE("--help is recognised") {
    const auto parsed = parse({"--help"});
    REQUIRE(parsed);
    CHECK(parsed->help);
}
