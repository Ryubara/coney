// SPDX-License-Identifier: GPL-3.0-or-later
// The console's own pad gate: SELECT and R3 open and close it, port 1 is held from the game while it is open and
// drives the command picker, and the buttons still held when it closes stay hidden until let go
// (docs/guides/console.md#front-end, docs/guides/studio/console.md#input).
#include "console/console_pad.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "console/command_spec.h"
#include "console/console.h"
#include "console/registry.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "support/test_paths.h"

using coney::PortSamples;
using coney::ScriptedInput;
using coney::console::CommandContext;
using coney::console::CommandSpec;
using coney::console::Console;
using coney::console::ConsolePad;
using coney::console::OpenedBy;
using coney::console::PickerPage;
namespace pad = coney::pad;

namespace {

// A console whose picker lists `help`, `version` and `missions`, each printing its name.
struct PadConsole {
    Console console{{}, {}};
    PadConsole() {
        const auto prints = [](std::string_view name) {
            return [name](CommandContext& context) -> std::expected<void, std::string> {
                context.print(std::string(name) + " ran");
                return {};
            };
        };
        auto& registry = console.registry();
        REQUIRE(
            registry.addBuiltin(CommandSpec{.name = "help", .group = "General", .summary = "Lists."}, prints("help")));
        REQUIRE(registry.addBuiltin(CommandSpec{.name = "version", .group = "General", .summary = "The version."},
                                    prints("version")));
        REQUIRE(registry.addBuiltin(
            CommandSpec{.name = "missions", .group = "Levels and missions", .summary = "The completed missions."},
            prints("missions")));
    }
};

// The script's events, from tests/support.
ScriptedInput script(const char* name) {
    auto events = coney::loadInputScript((coney::test::testSupportDir() / name).string());
    REQUIRE(events.has_value());
    return ScriptedInput(std::move(*events));
}

} // namespace

TEST_CASE("the pad chord opens the console, the picker runs missions, and the chord closes it again",
          "[console][pad]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PadConsole fixture;
    ScriptedInput input = script("console_pad_picker.txt");
    ConsolePad gate(&input, fixture.console);
    // What the game saw of port 1's buttons and port 2's, frame by frame.
    std::uint16_t seen[21]{};
    std::uint16_t seenPort2[21]{};
    bool openAt[21]{};
    for (std::uint64_t frame = 0; frame <= 20; ++frame) {
        const PortSamples samples = gate.sample(frame);
        CHECK(samples[0].connected);
        seen[frame] = samples[0].buttons;
        seenPort2[frame] = samples[1].buttons;
        openAt[frame] = gate.open();
    }
    // SELECT alone is the game's; once R3 joins it the console opens and the game sees nothing on port 1.
    CHECK(seen[2] == pad::kSelect);
    CHECK_FALSE(openAt[2]);
    CHECK(openAt[3]);
    for (int frame = 3; frame <= 13; ++frame) {
        CHECK(seen[frame] == 0);
        CHECK(openAt[frame]);
    }
    // The picker walked down twice to `missions`, and cross ran it as if typed.
    const auto& output = fixture.console.terminal().output();
    REQUIRE(output.size() == 2);
    CHECK(output[0].text == "> missions");
    CHECK(output[1].text == "missions ran");
    // The chord closes it; the chord's buttons stay hidden until let go, and the next cross is the game's.
    CHECK_FALSE(openAt[14]);
    CHECK(seen[14] == 0);
    CHECK(seen[15] == 0);
    CHECK(seen[20] == pad::kCross);
    // Port 2 reached the game throughout, the console open or not.
    CHECK(seenPort2[8] == pad::kCross);
    CHECK(seenPort2[12] == pad::kCross);
}

TEST_CASE("circle on the picker's commands closes the console, and stays hidden until let go", "[console][pad]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PadConsole fixture;
    auto events = coney::parseInputScript("1 press select r3\n2 release select r3\n4 press circle\n7 release circle\n");
    REQUIRE(events.has_value());
    ScriptedInput input(std::move(*events));
    ConsolePad gate(&input, fixture.console);
    for (std::uint64_t frame = 0; frame <= 3; ++frame) {
        (void)gate.sample(frame);
    }
    REQUIRE(gate.open());
    CHECK(gate.openedBy() == OpenedBy::Pad);
    CHECK(gate.picker().page() == PickerPage::Commands);
    CHECK(gate.picker().selected() == 0);
    CHECK(gate.sample(4)[0].buttons == 0);
    CHECK_FALSE(gate.open());
    CHECK(gate.sample(5)[0].buttons == 0);
    CHECK(gate.hiddenButtons() == pad::kCircle);
    CHECK(gate.sample(7)[0].buttons == 0);
    CHECK(gate.hiddenButtons() == 0);
}

TEST_CASE("opening from the keyboard holds port 1 too, and closing hides what is still held", "[console][pad]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    PadConsole fixture;
    auto events = coney::parseInputScript("1 press cross\n6 release cross\n");
    REQUIRE(events.has_value());
    ScriptedInput input(std::move(*events));
    ConsolePad gate(&input, fixture.console);
    CHECK(gate.sample(0)[0].buttons == 0);
    gate.setOpen(true);
    CHECK(gate.openedBy() == OpenedBy::Keyboard);
    // Cross held while open drives the picker (it runs `help`), never the game.
    CHECK(gate.sample(1)[0].buttons == 0);
    CHECK(gate.picker().page() == PickerPage::Commands);
    REQUIRE_FALSE(fixture.console.terminal().output().empty());
    CHECK(fixture.console.terminal().output().back().text == "help ran");
    gate.setOpen(false);
    CHECK(gate.sample(2)[0].buttons == 0);
    CHECK(gate.sample(6)[0].buttons == 0);
    CHECK(gate.hiddenButtons() == 0);
}
