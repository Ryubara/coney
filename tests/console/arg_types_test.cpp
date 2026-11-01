// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/arg_types.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "console/command_spec.h"

using coney::console::ArgSpec;
using coney::console::ArgType;
using coney::console::checkArg;
using coney::console::completeArg;
using coney::console::ValueSource;
using Strings = std::vector<std::string>;

namespace {

// A source with a fixed list for each game kind, and a checkpoint list that depends on the mission before it.
std::optional<Strings> fakeValues(ArgType type, std::span<const std::string> earlier) {
    switch (type) {
    case ArgType::Mission:
        return Strings{"1", "2", "3"};
    case ArgType::Checkpoint:
        if (!earlier.empty() && earlier.back() == "3") {
            return Strings{"1", "2", "4"};
        }
        return Strings{"1"};
    case ArgType::Setting:
        return Strings{"fps_cap", "show_fps", "vsync"};
    default:
        return std::nullopt;
    }
}

const ValueSource kValues = fakeValues;
const ValueSource kNoValues = [](ArgType, std::span<const std::string>) { return std::optional<Strings>(); };

// Whether `value` passes `arg`'s check with no earlier arguments.
bool passes(const ArgSpec& arg, std::string_view value, const ValueSource& values = kValues) {
    return checkArg(arg, value, {}, values).has_value();
}

} // namespace

TEST_CASE("an int argument takes a whole number and says what was wrong otherwise") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    const ArgSpec count{.name = "count", .type = ArgType::Int, .help = "How many"};
    CHECK(passes(count, "3"));
    CHECK(passes(count, "-12"));
    CHECK_FALSE(passes(count, "two"));
    CHECK_FALSE(passes(count, "1.5"));
    CHECK_FALSE(passes(count, ""));
    CHECK_FALSE(passes(count, "99999999999999999999999"));
    CHECK(checkArg(count, "two", {}, kValues).error() == R"("count": expected a whole number, got "two")");
}

TEST_CASE("a number argument takes a decimal") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    const ArgSpec scale{.name = "x", .type = ArgType::Number};
    CHECK(passes(scale, "0.5"));
    CHECK(passes(scale, "-2"));
    CHECK_FALSE(passes(scale, "half"));
    CHECK_FALSE(passes(scale, "nan"));
}

TEST_CASE("a bool argument takes on and off, and completes them") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    const ArgSpec flag{.name = "state", .type = ArgType::Bool};
    CHECK(passes(flag, "on"));
    CHECK(passes(flag, "off"));
    CHECK_FALSE(passes(flag, "yes"));
    CHECK(completeArg(flag, "", {}, kValues) == Strings{"off", "on"});
    CHECK(completeArg(flag, "of", {}, kValues) == Strings{"off"});
}

TEST_CASE("a text argument takes anything and completes nothing") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    const ArgSpec text{.name = "filter", .type = ArgType::Text};
    CHECK(passes(text, "level load; quit"));
    CHECK(passes(text, ""));
    CHECK(completeArg(text, "le", {}, kValues).empty());
}

TEST_CASE("a choice argument takes one of its list and completes from it") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    constexpr std::array<std::string_view, 3> kChoices{"record", "status", "save"};
    const ArgSpec action{.name = "action", .type = ArgType::Choice, .choices = kChoices};
    CHECK(passes(action, "status"));
    CHECK_FALSE(passes(action, "stop"));
    CHECK_THAT(checkArg(action, "stop", {}, kValues).error(),
               Catch::Matchers::ContainsSubstring("record, status, save"));
    CHECK(completeArg(action, "s", {}, kValues) == Strings{"save", "status"});
}

TEST_CASE("a mission argument takes a number of the host's list and completes from it") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    const ArgSpec mission{.name = "mission", .type = ArgType::Mission};
    CHECK(passes(mission, "2"));
    CHECK_FALSE(passes(mission, "7"));
    CHECK_FALSE(passes(mission, "0"));
    CHECK_FALSE(passes(mission, "two"));
    CHECK(completeArg(mission, "", {}, kValues) == Strings{"1", "2", "3"});
    // With no list known, any mission number passes and nothing completes.
    CHECK(passes(mission, "7", kNoValues));
    CHECK_FALSE(passes(mission, "0", kNoValues));
    CHECK(completeArg(mission, "", {}, kNoValues).empty());
}

TEST_CASE("a checkpoint argument reads the mission before it") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    const ArgSpec checkpoint{.name = "checkpoint", .type = ArgType::Checkpoint};
    const Strings third{"3"};
    CHECK(checkArg(checkpoint, "4", third, kValues).has_value());
    CHECK_FALSE(checkArg(checkpoint, "4", Strings{"1"}, kValues).has_value());
    CHECK(completeArg(checkpoint, "", third, kValues) == Strings{"1", "2", "4"});
}

TEST_CASE("a setting argument takes a name of the host's list and completes by prefix") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog
    const ArgSpec setting{.name = "setting", .type = ArgType::Setting};
    CHECK(passes(setting, "vsync"));
    CHECK_FALSE(passes(setting, "fov"));
    CHECK_FALSE(passes(setting, "Bad Name", kNoValues));
    CHECK(completeArg(setting, "s", {}, kValues) == Strings{"show_fps"});
}
