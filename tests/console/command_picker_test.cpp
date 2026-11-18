// SPDX-License-Identifier: GPL-3.0-or-later
// The gamepad's command picker: the commands by group, a row per argument filled by its type's completion or changed
// with left and right, and the line it builds run as if typed (docs/guides/console.md#front-end,
// docs/guides/studio/console.md#input).
#include "console/command_picker.h"

#include <array>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "console/command_spec.h"
#include "console/console.h"
#include "console/hosts.h"
#include "console/registry.h"

using coney::console::ArgSpec;
using coney::console::ArgType;
using coney::console::CommandContext;
using coney::console::CommandPicker;
using coney::console::CommandSpec;
using coney::console::Console;
using coney::console::Need;
using coney::console::PickerInput;
using coney::console::PickerPage;
using coney::console::PickerRow;
using coney::console::SessionHost;
using Strings = std::vector<std::string>;

namespace {

// A session host whose missions are 3 and 5, mission 5's checkpoints 1 and 2, and settings fps_cap and vsync.
class FakeSession final : public SessionHost {
  public:
    [[nodiscard]] bool has(Need /*need*/) const override { return true; }
    [[nodiscard]] std::optional<Strings> values(ArgType type, std::span<const std::string> earlier) const override {
        switch (type) {
        case ArgType::Mission:
            return Strings{"3", "5"};
        case ArgType::Checkpoint:
            return !earlier.empty() && earlier.front() == "5" ? Strings{"1", "2"} : Strings{"1"};
        case ArgType::Setting:
            return Strings{"fps_cap", "vsync"};
        default:
            return std::nullopt;
        }
    }
};

constexpr std::array<ArgSpec, 2> kMissionArgs{
    ArgSpec{.name = "mission", .type = ArgType::Mission, .help = "A completed mission"},
    ArgSpec{.name = "checkpoint", .type = ArgType::Checkpoint, .help = "Its checkpoint", .optional = true},
};
constexpr std::array<ArgSpec, 2> kSetArgs{
    ArgSpec{.name = "setting", .type = ArgType::Setting, .help = "A setting"},
    ArgSpec{.name = "value", .type = ArgType::Int, .help = "Its value", .optional = true, .defaultValue = "30"},
};

// A console whose commands are, in registration order: `help` (General), `missions` and `mission` (Levels and
// missions), `version` (General) and `set` (Settings). Each prints its name and arguments.
struct PickerConsole {
    FakeSession session;
    Console console{{}, {.session = &session}};
    PickerConsole() {
        const auto prints = [](std::string_view name) {
            return [name](CommandContext& context) -> std::expected<void, std::string> {
                std::string line(name);
                for (const std::string& value : context.args.values()) {
                    line += " " + value;
                }
                context.print(line + " ran");
                return {};
            };
        };
        auto& registry = console.registry();
        REQUIRE(
            registry.addBuiltin(CommandSpec{.name = "help", .group = "General", .summary = "Lists."}, prints("help")));
        REQUIRE(registry.addBuiltin(
            CommandSpec{.name = "missions", .group = "Levels and missions", .summary = "The completed missions."},
            prints("missions")));
        REQUIRE(registry.addBuiltin(
            CommandSpec{
                .name = "mission", .group = "Levels and missions", .summary = "Starts one.", .args = kMissionArgs},
            prints("mission")));
        REQUIRE(registry.addBuiltin(CommandSpec{.name = "version", .group = "General", .summary = "The version."},
                                    prints("version")));
        REQUIRE(registry.addBuiltin(
            CommandSpec{.name = "set", .group = "Settings", .summary = "Changes one.", .args = kSetArgs},
            prints("set")));
    }

    // The terminal's last line.
    [[nodiscard]] std::string lastLine() const {
        REQUIRE_FALSE(console.terminal().output().empty());
        return console.terminal().output().back().text;
    }
};

// The labels of `rows`.
Strings labels(const std::vector<PickerRow>& rows) {
    Strings out;
    for (const PickerRow& row : rows) {
        out.push_back(row.label);
    }
    return out;
}

// Presses `input` `times` times.
void press(CommandPicker& picker, PickerInput input, int times = 1) {
    for (int i = 0; i < times; ++i) {
        REQUIRE(picker.apply(input));
    }
}

} // namespace

TEST_CASE("the picker lists the commands by group, each group where its first command is", "[console][picker]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PickerConsole fixture;
    CommandPicker picker(fixture.console);
    CHECK(picker.page() == PickerPage::Commands);
    const std::vector<PickerRow> rows = picker.rows();
    CHECK(labels(rows) == Strings{"help", "version", "missions", "mission", "set"});
    REQUIRE(rows.size() == 5);
    CHECK(rows[1].value == "General");
    CHECK(rows[2].value == "Levels and missions");
    CHECK(rows[2].help == "The completed missions.");
    CHECK(picker.selected() == 0);
}

TEST_CASE("cross on a command without arguments runs it as if typed", "[console][picker]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PickerConsole fixture;
    CommandPicker picker(fixture.console);
    press(picker, PickerInput::Down, 2);
    press(picker, PickerInput::Accept);
    CHECK(fixture.lastLine() == "missions ran");
    CHECK(fixture.console.terminal().output().front().text == "> missions");
    CHECK(picker.page() == PickerPage::Commands);
    CHECK(picker.line().empty());
}

TEST_CASE("the arguments' rows take their values from completion, and the last row runs the line",
          "[console][picker]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PickerConsole fixture;
    CommandPicker picker(fixture.console);
    press(picker, PickerInput::Down, 3);
    press(picker, PickerInput::Accept);
    REQUIRE(picker.page() == PickerPage::Arguments);
    CHECK(labels(picker.rows()) == Strings{"mission", "checkpoint", "run"});
    // A required argument starts at its first value, an optional one left out.
    CHECK(picker.line() == "mission 3");
    press(picker, PickerInput::Right);
    CHECK(picker.line() == "mission 5");
    // Past the end the list wraps round.
    press(picker, PickerInput::Right);
    CHECK(picker.line() == "mission 3");
    press(picker, PickerInput::Left);
    CHECK(picker.line() == "mission 5");
    // Cross goes on to the checkpoint, whose list follows the mission chosen.
    press(picker, PickerInput::Accept);
    CHECK(picker.selected() == 1);
    press(picker, PickerInput::Right, 2);
    CHECK(picker.line() == "mission 5 2");
    press(picker, PickerInput::Down);
    press(picker, PickerInput::Accept);
    CHECK(fixture.lastLine() == "mission 5 2 ran");
    CHECK(picker.page() == PickerPage::Commands);
}

TEST_CASE("a number with no list changes by one with left and right, from its default", "[console][picker]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PickerConsole fixture;
    CommandPicker picker(fixture.console);
    press(picker, PickerInput::Down, 4);
    press(picker, PickerInput::Accept);
    REQUIRE(picker.page() == PickerPage::Arguments);
    CHECK(picker.line() == "set fps_cap 30");
    press(picker, PickerInput::Down);
    press(picker, PickerInput::Right, 3);
    press(picker, PickerInput::Left);
    CHECK(picker.line() == "set fps_cap 32");
    press(picker, PickerInput::Down);
    press(picker, PickerInput::Accept);
    CHECK(fixture.lastLine() == "set fps_cap 32 ran");
}

TEST_CASE("circle goes back to the commands, and on the commands asks to close", "[console][picker]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PickerConsole fixture;
    CommandPicker picker(fixture.console);
    press(picker, PickerInput::Down, 3);
    press(picker, PickerInput::Accept);
    REQUIRE(picker.page() == PickerPage::Arguments);
    press(picker, PickerInput::Back);
    CHECK(picker.page() == PickerPage::Commands);
    CHECK(picker.selected() == 3);
    CHECK_FALSE(picker.apply(PickerInput::Back));
    // Up from the first row stays there, and reset() goes back to it.
    press(picker, PickerInput::Up, 9);
    CHECK(picker.selected() == 0);
    press(picker, PickerInput::Down, 2);
    picker.reset();
    CHECK(picker.selected() == 0);
    CHECK(fixture.console.terminal().output().empty());
}
